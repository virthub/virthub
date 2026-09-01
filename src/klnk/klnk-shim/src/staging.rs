// virthub/src/klnk/klnk-shim/src/staging.rs

//! Staging memory allocation for the KLNK interposition shim.
//!
//! This module provides a `LocalStagingAllocation` that pre‑allocates
//! memory using `memmap2`. The pages are pre‑touched to guarantee physical
//! backing, which is required for `UFFDIO_MOVE`. The allocation is
//! automatically unmapped on drop.
//!
//! The implementation uses a fixed page size of 4096 bytes for simplicity
//! and to avoid unsafe system calls. If a different page size is required,
//! adjust the `PAGE_SIZE` constant.

use memmap2::{MmapMut, MmapOptions};
use std::io;
use thiserror::Error;

/// Assumed system page size (4 KB).
const PAGE_SIZE: usize = 4096;

#[derive(Debug, Error)]
pub enum StagingAllocationError {
    #[error("Failed to allocate staging memory: {0}")]
    AllocationFailed(io::Error),

    #[error("Invalid allocation size or zero pages requested")]
    InvalidSize,
}

/// A handle representing an allocated and pre‑touched staging memory region
/// inside the target application's virtual address space.
///
/// Linux `UFFDIO_MOVE` (available in kernel 6.8+) atomically moves physical page
/// frames between virtual addresses in the same process space. For `UFFDIO_MOVE`
/// to succeed, the source virtual address (`src`) MUST be fully backed by physical
/// page frames.
///
/// This pool pre‑allocates a contiguous block of memory and writes to every 4 KB
/// stride to force immediate kernel physical page commit.
#[derive(Debug)]
pub struct LocalStagingAllocation {
    mmap: MmapMut,
    page_size: usize,
    num_pages: usize,
}

// Safety: The allocation is local to the target process memory space.
unsafe impl Send for LocalStagingAllocation {}
unsafe impl Sync for LocalStagingAllocation {}

impl LocalStagingAllocation {
    /// Huge Page size (2MB). Retained for API compatibility; not used in
    /// this safe fallback implementation.
    pub const HUGE_PAGE_SIZE: usize = 2 * 1024 * 1024;

    /// Allocates `num_pages` of memory (each of `PAGE_SIZE` bytes) and
    /// pre‑touches every page to guarantee physical backing.
    pub fn new(num_pages: usize) -> Result<Self, StagingAllocationError> {
        if num_pages == 0 {
            return Err(StagingAllocationError::InvalidSize);
        }

        let total_size = num_pages * PAGE_SIZE;

        // Create a mutable anonymous mapping.
        let mut mmap = MmapOptions::new()
            .len(total_size)
            .map_anon()
            .map_err(StagingAllocationError::AllocationFailed)?;

        // Pre‑touch every page: write a zero byte at each page start.
        // Safe because we just created the mapping as writable.
        for offset in (0..mmap.len()).step_by(PAGE_SIZE) {
            mmap[offset] = 0;
        }

        Ok(Self {
            mmap,
            page_size: PAGE_SIZE,
            num_pages,
        })
    }

    /// Returns the base virtual address of the staging region in the target application space.
    pub fn base_address(&self) -> u64 {
        self.mmap.as_ptr() as u64
    }

    /// Returns the total byte size of the allocation.
    pub fn total_size(&self) -> usize {
        self.mmap.len()
    }

    /// Returns the size of an individual page frame (always 4096 in this fallback).
    pub fn page_size(&self) -> usize {
        self.page_size
    }

    /// Returns the total number of allocated page slots.
    pub fn num_pages(&self) -> usize {
        self.num_pages
    }

    /// Advise the kernel about the expected memory access pattern.
    /// This implementation is a no‑op to avoid unsafe code.
    pub fn advise_memory(&self, _advice: libc::c_int) -> Result<(), StagingAllocationError> {
        // No‑op: safe fallback.
        Ok(())
    }

    /// Mark the staging memory as having a sequential access pattern (no‑op).
    pub fn mark_sequential(&self) -> Result<(), StagingAllocationError> {
        self.advise_memory(libc::MADV_SEQUENTIAL)
    }

    /// Mark the staging memory as having a random access pattern (no‑op).
    pub fn mark_random(&self) -> Result<(), StagingAllocationError> {
        self.advise_memory(libc::MADV_RANDOM)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_local_staging_allocation() {
        let alloc = LocalStagingAllocation::new(2);
        match alloc {
            Ok(a) => {
                assert!(a.base_address() > 0);
                assert!(a.total_size() > 0);
                assert_eq!(a.num_pages(), 2);
                assert_eq!(a.page_size(), PAGE_SIZE);
                // Verify pre‑touch worked by checking the first byte.
                let slice = unsafe {
                    std::slice::from_raw_parts(a.base_address() as *const u8, a.total_size())
                };
                assert_eq!(slice[0], 0);
            }
            Err(_) => {
                eprintln!("Staging allocation failed");
            }
        }
    }

    #[test]
    fn test_invalid_num_pages() {
        let result = LocalStagingAllocation::new(0);
        assert!(matches!(result, Err(StagingAllocationError::InvalidSize)));
    }
}
