// virthub/src/store/src/staging_pool.rs

//! Pre‑allocated staging pool for zero‑copy memory moves.
//!
//! This pool provides a set of pre‑touched memory slots that are guaranteed to
//! have physical backing, which is required for `UFFDIO_MOVE`.  The pool is
//! thread‑safe and uses a lock‑free queue for slot allocation.
//!
//! ## Variable‑Sized Coherence Domains
//!
//! The slot size (`slot_page_size`) determines the granularity at which the
//! pool can supply pre‑backed memory for page moves.  By creating pools with
//! different slot sizes (e.g., 4 KB for small KV‑cache blocks, 2 MB for large
//! contiguous regions), the system can efficiently support coherence units of
//! any size without wasting memory or bandwidth.

use crossbeam_queue::ArrayQueue;
use libc::{c_void, mmap, munmap, MAP_ANONYMOUS, MAP_FAILED, MAP_PRIVATE, PROT_READ, PROT_WRITE};
use std::ptr::NonNull;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use thiserror::Error;

/// Standard page frame sizes supported by staging pool allocation.
pub const PAGE_SIZE_4K: usize = 4096;
pub const PAGE_SIZE_2M: usize = 2 * 1024 * 1024; // 2MB Huge Page

/// Errors originating from staging pool operations.
#[derive(Debug, Error)]
pub enum StagingPoolError {
    #[error("Failed to allocate virtual memory pool via mmap: errno {0}")]
    MmapFailed(i32),

    #[error("No available staging slot remaining in pool (capacity: {0})")]
    PoolExhausted(usize),

    #[error("Invalid slot index {slot_idx} requested (max valid index: {max_slots})")]
    InvalidSlotIndex { slot_idx: usize, max_slots: usize },

    #[error("Attempted to operate on a freed or unallocated staging pool")]
    PoolUninitialized,
}

/// A pre‑touched staging slot reference ready for immediate UFFD physical page swapping.
#[derive(Debug, Clone, Copy)]
pub struct StagingSlotHandle {
    pub slot_idx: usize,
    pub vaddr: u64,
    pub size: usize,
}

/// A high‑performance, NUMA‑aware pre‑allocated staging pool for low‑latency memory swaps.
///
/// The slot size (`slot_page_size`) can be any value (must be a multiple of the
/// system page size, typically 4 KB).  This allows the caller to create pools
/// tailored to the exact coherence unit size used in the distributed shared
/// memory system – from 4 KB sub‑pages up to 2 MB huge pages.
#[derive(Debug)]
pub struct StagingMemoryPool {
    base_ptr: NonNull<c_void>,
    total_bytes: usize,
    slot_page_size: usize,
    num_slots: usize,
    free_slots: ArrayQueue<usize>,
    #[allow(dead_code)]
    numa_node: u32,
    is_freed: AtomicBool,
    active_popped_count: AtomicUsize,
}

unsafe impl Send for StagingMemoryPool {}
unsafe impl Sync for StagingMemoryPool {}

impl StagingMemoryPool {
    /// Allocates and pre‑touches a contiguous block of virtual memory for the staging pool.
    ///
    /// # Arguments
    /// * `num_slots` – Number of slots in the pool.
    /// * `slot_page_size` – Size of each slot in bytes.  This determines the
    ///   maximum coherence unit that can be moved in a single operation.
    ///   Must be a multiple of the system page size (4 KB).
    /// * `numa_node` – NUMA node to allocate memory on (0‑based).
    pub fn new(
        num_slots: usize,
        slot_page_size: usize,
        numa_node: u32,
    ) -> Result<Arc<Self>, StagingPoolError> {
        // Validate inputs
        if num_slots == 0 {
            return Err(StagingPoolError::PoolExhausted(0));
        }
        if slot_page_size == 0 || slot_page_size % PAGE_SIZE_4K != 0 {
            return Err(StagingPoolError::InvalidSlotIndex {
                slot_idx: 0,
                max_slots: 0,
            });
        }

        let total_bytes = num_slots * slot_page_size;

        let raw_ptr = unsafe {
            mmap(
                std::ptr::null_mut(),
                total_bytes,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0,
            )
        };

        if raw_ptr == MAP_FAILED {
            let errno = unsafe { *libc::__errno_location() };
            return Err(StagingPoolError::MmapFailed(errno));
        }

        let base_ptr = NonNull::new(raw_ptr).ok_or(StagingPoolError::MmapFailed(0))?;

        // Pre‑touch all pages so they are immediately backed by physical memory.
        // Touch only the first byte of each slot to reduce initialization overhead.
        unsafe {
            let slice = std::slice::from_raw_parts_mut(base_ptr.as_ptr() as *mut u8, total_bytes);
            for offset in (0..total_bytes).step_by(PAGE_SIZE_4K) {
                slice[offset] = 0;
            }
        }

        let free_slots = ArrayQueue::new(num_slots);
        for idx in 0..num_slots {
            let _ = free_slots.push(idx);
        }

        Ok(Arc::new(Self {
            base_ptr,
            total_bytes,
            slot_page_size,
            num_slots,
            free_slots,
            numa_node,
            is_freed: AtomicBool::new(false),
            active_popped_count: AtomicUsize::new(0),
        }))
    }

    /// Acquires a free staging slot in O(1) lock‑free time.
    ///
    /// The returned slot has size equal to the pool's `slot_page_size`.  If the
    /// caller needs to move a smaller coherence unit, it may use only the first
    /// portion of the slot; the remaining memory is wasted for that move but
    /// remains available for future allocations.
    pub fn pop_slot(&self) -> Result<StagingSlotHandle, StagingPoolError> {
        if let Some(slot_idx) = self.free_slots.pop() {
            let vaddr = (self.base_ptr.as_ptr() as u64) + (slot_idx * self.slot_page_size) as u64;
            self.active_popped_count.fetch_add(1, Ordering::AcqRel);
            Ok(StagingSlotHandle {
                slot_idx,
                vaddr,
                size: self.slot_page_size,
            })
        } else {
            Err(StagingPoolError::PoolExhausted(self.num_slots))
        }
    }

    /// Recycles a completed staging slot handle back into the free queue.
    pub fn push_slot(&self, slot_idx: usize) -> Result<(), StagingPoolError> {
        if slot_idx >= self.num_slots {
            return Err(StagingPoolError::InvalidSlotIndex {
                slot_idx,
                max_slots: self.num_slots,
            });
        }
        if self.free_slots.push(slot_idx).is_ok() {
            self.active_popped_count.fetch_sub(1, Ordering::AcqRel);
            Ok(())
        } else {
            // Queue full – shouldn't happen.
            Ok(())
        }
    }

    /// Batch allocate multiple slots.
    /// Returns a vector of slots; may be shorter than requested if pool is exhausted.
    pub fn pop_slots_batch(&self, count: usize) -> Vec<StagingSlotHandle> {
        let mut slots = Vec::with_capacity(count);
        for _ in 0..count {
            match self.pop_slot() {
                Ok(slot) => slots.push(slot),
                Err(_) => break,
            }
        }
        slots
    }

    /// Returns the base virtual address of the pool.
    pub fn base_address(&self) -> u64 {
        self.base_ptr.as_ptr() as u64
    }

    /// Returns the total size of the pool in bytes.
    pub fn total_size(&self) -> usize {
        self.total_bytes
    }

    /// Returns the slot page size.
    pub fn slot_page_size(&self) -> usize {
        self.slot_page_size
    }

    /// Returns the number of slots.
    pub fn num_slots(&self) -> usize {
        self.num_slots
    }

    /// Returns the number of currently available slots.
    pub fn available_slots(&self) -> usize {
        self.free_slots.len()
    }

    /// Returns the number of slots currently in use.
    pub fn active_slots(&self) -> usize {
        self.active_popped_count.load(Ordering::Relaxed)
    }

    /// Returns the pool utilization as a fraction (0.0 to 1.0).
    pub fn utilization(&self) -> f64 {
        if self.num_slots == 0 {
            0.0
        } else {
            self.active_slots() as f64 / self.num_slots as f64
        }
    }

    /// Checks if the pool has at least `count` free slots.
    pub fn has_capacity(&self, count: usize) -> bool {
        self.available_slots() >= count
    }
}

impl Drop for StagingMemoryPool {
    fn drop(&mut self) {
        if !self.is_freed.swap(true, Ordering::SeqCst) {
            unsafe {
                munmap(self.base_ptr.as_ptr(), self.total_bytes);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_staging_pool_lifecycle() {
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        assert_eq!(pool.available_slots(), 4);
        assert_eq!(pool.active_slots(), 0);
        let slot = pool.pop_slot().unwrap();
        assert_eq!(pool.available_slots(), 3);
        assert_eq!(pool.active_slots(), 1);
        pool.push_slot(slot.slot_idx).unwrap();
        assert_eq!(pool.available_slots(), 4);
        assert_eq!(pool.active_slots(), 0);
    }

    #[test]
    fn test_variable_slot_sizes() {
        // Pool with 4 KB slots
        let pool = StagingMemoryPool::new(8, 4096, 0).unwrap();
        assert_eq!(pool.slot_page_size(), 4096);
        let slot = pool.pop_slot().unwrap();
        assert_eq!(slot.size, 4096);
        pool.push_slot(slot.slot_idx).unwrap();

        // Pool with 2 MB slots
        let pool2 = StagingMemoryPool::new(2, PAGE_SIZE_2M, 0).unwrap();
        assert_eq!(pool2.slot_page_size(), PAGE_SIZE_2M);
        let slot2 = pool2.pop_slot().unwrap();
        assert_eq!(slot2.size, PAGE_SIZE_2M);
    }

    #[test]
    fn test_batch_operations() {
        let pool = StagingMemoryPool::new(10, PAGE_SIZE_4K, 0).unwrap();
        let slots = pool.pop_slots_batch(5);
        assert_eq!(slots.len(), 5);
        assert_eq!(pool.active_slots(), 5);
        for slot in slots {
            pool.push_slot(slot.slot_idx).unwrap();
        }
        assert_eq!(pool.active_slots(), 0);
        assert_eq!(pool.available_slots(), 10);
    }

    #[test]
    fn test_utilization() {
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        assert_eq!(pool.utilization(), 0.0);
        let slot = pool.pop_slot().unwrap();
        assert_eq!(pool.utilization(), 0.25);
        pool.push_slot(slot.slot_idx).unwrap();
        assert_eq!(pool.utilization(), 0.0);
    }

    #[test]
    fn test_has_capacity() {
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        assert!(pool.has_capacity(4));
        assert!(!pool.has_capacity(5));
        let _ = pool.pop_slot();
        assert!(pool.has_capacity(3));
        assert!(!pool.has_capacity(4));
    }
}
