// virthub/src/klnk/klnk-daemon/src/invalidation.rs

//! Invalidation buffer handling for RDMA‑based invalidation messages.
//!
//! This module provides a **lock‑free ring buffer** that is exposed via RDMA
//! for receiving invalidation messages (page address + new version + size) from
//! remote writers.  A background poller reads the buffer and updates local page
//! states accordingly.
//!
//! ## Atomic Tail Update
//!
//! The tail index is stored as an `AtomicU64` and updated with compare‑and‑swap
//! (CAS).  This allows **multiple remote writers** to safely append entries
//! without CPU‑side locking on the local node.  Remote writers issue RDMA atomic
//! CAS operations on the tail index, then write the entry payload via RDMA write.
//!
//! ## Variable‑Sized Coherence Entries
//!
//! Each invalidation message now includes a `page_size` field.  This allows the
//! system to invalidate coherence units of any size (e.g., 4 KB, 128 KB, 2 MB),
//! supporting the variable‑sized coherence domains described in the KLNK paper.
//!
//! ## Lazy Self‑Invalidation Compatibility
//!
//! When the control plane adopts lazy self‑invalidation (readers check version
//! before using a cached page), the invalidation buffer becomes a **notification
//! channel** for metadata updates.  The poller can be repurposed to trigger
//! version re‑checks, or left idle if readers poll remote metadata directly.

use klnk_core::control_plane::ControlPlaneManager;
use klnk_core::domain::PageCoherenceState;
use librmashim::RmaTransportEngine;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tracing::{debug, info, warn};

/// Each entry is 32 bytes: valid (u64), vaddr (u64), version (u64), page_size (u64).
const ENTRY_SIZE: usize = 32;
const NUM_ENTRIES: usize = 1024;
/// Total buffer size: head (8) + tail (8) + entries (NUM_ENTRIES * ENTRY_SIZE).
pub const INVAL_BUFFER_SIZE: usize = 16 + (NUM_ENTRIES * ENTRY_SIZE);

/// A lock‑free ring buffer for invalidation messages, exposed via RDMA.
///
/// The buffer layout is:
///   - Offset 0:  head index (AtomicU64) – advanced by the local poller
///   - Offset 8:  tail index (AtomicU64) – advanced by remote writers via CAS
///   - Offset 16: entry array (NUM_ENTRIES × ENTRY_SIZE)
pub struct InvalidationBuffer {
    /// Pointer to the start of the buffer (allocated with 64‑byte alignment).
    ptr: *mut u8,
    /// Total buffer size (should equal INVAL_BUFFER_SIZE).
    size: usize,
    /// Virtual address of the buffer (for RDMA registration).
    vaddr: u64,
}

unsafe impl Send for InvalidationBuffer {}
unsafe impl Sync for InvalidationBuffer {}

impl InvalidationBuffer {
    /// Creates a new invalidation buffer by allocating a zeroed memory region.
    pub fn new() -> Self {
        let size = INVAL_BUFFER_SIZE;
        let layout = std::alloc::Layout::from_size_align(size, 64).unwrap();
        let ptr = unsafe { std::alloc::alloc_zeroed(layout) };
        if ptr.is_null() {
            panic!("Failed to allocate invalidation buffer");
        }
        let vaddr = ptr as u64;
        Self { ptr, size, vaddr }
    }

    /// Returns the virtual address of the buffer.
    pub fn vaddr(&self) -> u64 {
        self.vaddr
    }

    /// Returns the size of the buffer.
    pub fn size(&self) -> usize {
        self.size
    }

    /// Returns a reference to the head atomic (offset 0).
    fn head_atomic(&self) -> &AtomicU64 {
        unsafe { &*(self.ptr as *const AtomicU64) }
    }

    /// Returns a reference to the tail atomic (offset 8).
    fn tail_atomic(&self) -> &AtomicU64 {
        unsafe { &*((self.ptr as *const u8).add(8) as *const AtomicU64) }
    }

    /// Reads the head index (the next entry to read).
    pub fn head(&self) -> u64 {
        self.head_atomic().load(Ordering::Acquire)
    }

    /// Writes the head index (only called by the local poller).
    fn set_head(&self, head: u64) {
        self.head_atomic().store(head, Ordering::Release);
    }

    /// Reads the tail index (the next entry to write).
    pub fn tail(&self) -> u64 {
        self.tail_atomic().load(Ordering::Acquire)
    }

    /// Attempts to advance the tail index from `expected` to `new_val` atomically.
    ///
    /// This is intended to be called by a **remote writer** via an RDMA atomic
    /// compare‑and‑swap operation.  The local daemon does not write the tail
    /// directly – it only reads it.
    ///
    /// Returns `Ok(previous_tail)` if the CAS succeeded, or `Err(current_tail)` if
    /// the tail was modified concurrently.
    #[allow(dead_code)]
    pub fn advance_tail_atomic(&self, expected: u64, new_val: u64) -> Result<u64, u64> {
        self.tail_atomic()
            .compare_exchange(expected, new_val, Ordering::AcqRel, Ordering::Acquire)
            .map_err(|actual| actual)
    }

    /// Reads an entry at index `idx` (returns (valid, vaddr, version, page_size)).
    fn read_entry_raw(&self, idx: u64) -> (u64, u64, u64, u64) {
        let offset = 16 + (idx % NUM_ENTRIES as u64) as usize * ENTRY_SIZE;
        unsafe {
            // Ensure previous writes are visible before reading the entry.
            std::sync::atomic::fence(Ordering::Acquire);
            let valid = self.ptr.add(offset).cast::<u64>().read_volatile();
            let vaddr = self.ptr.add(offset + 8).cast::<u64>().read_volatile();
            let version = self.ptr.add(offset + 16).cast::<u64>().read_volatile();
            let page_size = self.ptr.add(offset + 24).cast::<u64>().read_volatile();
            (valid, vaddr, version, page_size)
        }
    }

    /// Writes an entry at index `idx` (local operation, only used in tests).
    fn write_entry_raw(&self, idx: u64, valid: u64, vaddr: u64, version: u64, page_size: u64) {
        let offset = 16 + (idx % NUM_ENTRIES as u64) as usize * ENTRY_SIZE;
        unsafe {
            self.ptr.add(offset).cast::<u64>().write_volatile(valid);
            self.ptr.add(offset + 8).cast::<u64>().write_volatile(vaddr);
            self.ptr.add(offset + 16).cast::<u64>().write_volatile(version);
            self.ptr.add(offset + 24).cast::<u64>().write_volatile(page_size);
            std::sync::atomic::fence(Ordering::Release);
        }
    }

    /// Processes all pending invalidation messages, calling a callback for each.
    ///
    /// The callback receives `(vaddr, new_version, page_size)` and should return
    /// `true` if the entry was processed successfully.  Processed entries are
    /// cleared (valid flag set to 0) and the head pointer is advanced.
    pub fn process_pending<F>(&self, mut callback: F) -> usize
    where
        F: FnMut(u64, u64, u64) -> bool, // (vaddr, version, page_size) -> success
    {
        let mut processed = 0;
        let head = self.head();
        let tail = self.tail();

        let mut idx = head;
        while idx < tail {
            let (valid, vaddr, version, page_size) = self.read_entry_raw(idx);
            if valid == 0 {
                // Entry not yet fully written or already consumed; stop.
                break;
            }
            if callback(vaddr, version, page_size) {
                // Clear the entry after successful processing.
                self.write_entry_raw(idx, 0, 0, 0, 0);
                processed += 1;
                idx += 1;
            } else {
                // Callback rejected; keep entry and stop to avoid reordering issues.
                break;
            }
        }

        if processed > 0 {
            self.set_head(idx);
        }
        processed
    }

    /// Writes an invalidation message (called by the writer on the remote node's
    /// buffer).  This is used only in tests; real remote writers use RDMA writes
    /// directly to the buffer after atomically advancing the tail.
    #[cfg(test)]
    pub fn write_entry(&self, vaddr: u64, version: u64, page_size: u64) -> bool {
        let tail = self.tail();
        if tail - self.head() >= NUM_ENTRIES as u64 {
            warn!("Invalidation buffer full; dropping message");
            return false;
        }

        self.write_entry_raw(tail, 1, vaddr, version, page_size);
        self.tail_atomic().store(tail + 1, Ordering::Release);
        true
    }
}

impl Drop for InvalidationBuffer {
    fn drop(&mut self) {
        let layout = std::alloc::Layout::from_size_align(self.size, 64).unwrap();
        unsafe {
            std::alloc::dealloc(self.ptr, layout);
        }
    }
}

/// Starts a background task that polls the invalidation buffer and updates page states.
///
/// When an invalidation message is received, the local page state is marked as
/// `Invalid` and the local node is removed from the readers list.  The caller
/// can then re‑fetch the page from the writer on the next access.
///
/// The poller now uses an adaptive sleep: when the buffer is empty, it sleeps
/// for `IDLE_SLEEP_MS` (default 1ms). When entries are processed, it does not
/// sleep, allowing rapid draining of bursts.
pub async fn start_invalidation_poller(
    control_plane: Arc<ControlPlaneManager>,
    _rma_engine: Arc<RmaTransportEngine>,
    inval_buffer: Arc<InvalidationBuffer>,
) {
    info!("Invalidation poller started.");
    let local_node = control_plane.local_node_id();

    const IDLE_SLEEP_MS: u64 = 1;   // 1 ms when no work

    loop {
        let mut processed = 0usize;
        let head_before = inval_buffer.head();
        let tail_before = inval_buffer.tail();

        // Process all currently available entries in a batch.
        if tail_before > head_before {
            processed = inval_buffer.process_pending(|vaddr, new_version, page_size| {
                // Use the exact base address for lookup (variable‑sized coherence).
                if let Some(entry) = control_plane.lookup_page_state(vaddr) {
                    if entry.primary_owner != local_node {
                        // Mark as Invalid so next access triggers a re‑fetch.
                        control_plane.update_page_state(
                            vaddr,
                            PageCoherenceState::Invalid,
                            entry.primary_owner,
                        );
                        control_plane.remove_reader(vaddr, local_node);
                        debug!(
                            "Invalidated page 0x{:x} (size {}), new version {}",
                            vaddr, page_size, new_version
                        );
                        true
                    } else {
                        warn!(
                            "Received invalidation for own page 0x{:x}; ignoring",
                            vaddr
                        );
                        false
                    }
                } else {
                    warn!("Invalidation for unknown page 0x{:x}; ignoring", vaddr);
                    false
                }
            });
        }

        if processed > 0 {
            debug!("Processed {} invalidation messages", processed);
            // No sleep when active; loop immediately to drain remaining entries.
            tokio::task::yield_now().await;
        } else {
            tokio::time::sleep(Duration::from_millis(IDLE_SLEEP_MS)).await;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_invalidation_buffer_write_read() {
        let buf = InvalidationBuffer::new();
        assert!(buf.write_entry(0x1000, 42, 4096));
        assert!(buf.write_entry(0x2000, 43, 2 * 1024 * 1024));

        let mut processed = 0;
        buf.process_pending(|vaddr, version, page_size| {
            if processed == 0 {
                assert_eq!(vaddr, 0x1000);
                assert_eq!(version, 42);
                assert_eq!(page_size, 4096);
            } else {
                assert_eq!(vaddr, 0x2000);
                assert_eq!(version, 43);
                assert_eq!(page_size, 2 * 1024 * 1024);
            }
            processed += 1;
            true
        });
        assert_eq!(processed, 2);

        // Buffer should now be empty.
        buf.process_pending(|_, _, _| {
            panic!("Should not be called");
        });
    }

    #[test]
    fn test_atomic_tail_advance() {
        let buf = InvalidationBuffer::new();
        let initial_tail = buf.tail();
        assert_eq!(initial_tail, 0);

        // Simulate a remote writer advancing the tail from 0 to 1
        let result = buf.advance_tail_atomic(0, 1);
        assert_eq!(result, Ok(0));
        assert_eq!(buf.tail(), 1);

        // A concurrent write that tries to advance from 0 should fail
        let result2 = buf.advance_tail_atomic(0, 2);
        assert_eq!(result2, Err(1));
        assert_eq!(buf.tail(), 1);
    }

    #[test]
    fn test_buffer_full() {
        let buf = InvalidationBuffer::new();
        // Fill the buffer
        for i in 0..NUM_ENTRIES {
            assert!(buf.write_entry(i as u64 * 0x1000, i as u64, 4096));
        }
        // Next write should fail
        assert!(!buf.write_entry(0xFFFF_0000, 999, 4096));
    }
}
