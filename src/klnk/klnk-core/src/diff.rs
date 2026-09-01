// virthub/src/klnk/klnk-core/src/diff.rs

use serde::{Deserialize, Serialize};
use thiserror::Error;

/// Error types for page diff and patch operations.
#[derive(Debug, Error)]
pub enum DiffError {
    #[error("Page size mismatch between dirty page ({dirty_len}) and baseline snapshot ({base_len})")]
    SizeMismatch { dirty_len: usize, base_len: usize },

    #[error("Patch offset {offset} with length {len} exceeds target page boundary ({page_len})")]
    OutOfBoundsPatch {
        offset: usize,
        len: usize,
        page_len: usize,
    },

    #[error("Buffer length ({0}) is not 8-byte aligned for word-level comparison")]
    UnalignedBuffer(usize),
}

/// A contiguous byte delta range inside a modified page frame.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PageDiffChunk {
    /// Starting byte offset inside the page frame
    pub offset: u32,
    /// Byte contents of the modification
    pub bytes: Vec<u8>,
}

/// Compact list of modifications representing the difference between a baseline snapshot and a modified page.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PageDiffList {
    /// Base virtual address of the target page
    pub page_vaddr: u64,
    /// Total byte size of the page (typically 4096 or 2097152)
    pub page_size: usize,
    /// List of modified byte chunks
    pub chunks: Vec<PageDiffChunk>,
}

impl PageDiffList {
    /// Calculates total size in bytes of all modified chunks combined.
    pub fn total_modified_bytes(&self) -> usize {
        self.chunks.iter().map(|c| c.bytes.len()).sum()
    }

    /// Evaluates if the page contains any modifications.
    pub fn is_empty(&self) -> bool {
        self.chunks.is_empty()
    }
}

/// High-performance memory diff engine utilizing 64-bit word comparisons.
pub struct MemoryDiffEngine;

impl MemoryDiffEngine {
    /// Compares a dirty page slice against a baseline reference snapshot and returns
    /// a compressed list of contiguous modified byte chunks.
    ///
    /// # Performance
    /// Performs 64-bit word-level scanning to skip unchanged regions in $O(N/8)$ iterations.
    pub fn compute_diff(
        page_vaddr: u64,
        dirty_page: &[u8],
        baseline_page: &[u8],
    ) -> Result<PageDiffList, DiffError> {
        if dirty_page.len() != baseline_page.len() {
            return Err(DiffError::SizeMismatch {
                dirty_len: dirty_page.len(),
                base_len: baseline_page.len(),
            });
        }

        let page_size = dirty_page.len();
        let mut chunks = Vec::new();

        // Process in 8-byte word strides for speed
        let word_stride = 8;
        let word_count = page_size / word_stride;

        let dirty_words: &[u64] = unsafe {
            std::slice::from_raw_parts(dirty_page.as_ptr() as *const u64, word_count)
        };
        let base_words: &[u64] = unsafe {
            std::slice::from_raw_parts(baseline_page.as_ptr() as *const u64, word_count)
        };

        let mut word_idx = 0;
        while word_idx < word_count {
            if dirty_words[word_idx] != base_words[word_idx] {
                // Modified chunk found: find contiguous modified region
                let chunk_start_bytes = word_idx * word_stride;
                let mut chunk_end_bytes = chunk_start_bytes + word_stride;

                // Look ahead to merge contiguous modified words
                word_idx += 1;
                while word_idx < word_count && dirty_words[word_idx] != base_words[word_idx] {
                    chunk_end_bytes += word_stride;
                    word_idx += 1;
                }

                // Trim leading matching bytes within the first modified word
                let mut real_start = chunk_start_bytes;
                while real_start < chunk_end_bytes && dirty_page[real_start] == baseline_page[real_start] {
                    real_start += 1;
                }

                // Trim trailing matching bytes within the last modified word
                let mut real_end = chunk_end_bytes;
                while real_end > real_start && dirty_page[real_end - 1] == baseline_page[real_end - 1] {
                    real_end -= 1;
                }

                if real_start < real_end {
                    chunks.push(PageDiffChunk {
                        offset: real_start as u32,
                        bytes: dirty_page[real_start..real_end].to_vec(),
                    });
                }
            } else {
                word_idx += 1;
            }
        }

        // Handle remaining tail bytes if page size is not divisible by 8
        let processed_tail = word_count * word_stride;
        if processed_tail < page_size {
            for i in processed_tail..page_size {
                if dirty_page[i] != baseline_page[i] {
                    chunks.push(PageDiffChunk {
                        offset: i as u32,
                        bytes: vec![dirty_page[i]],
                    });
                }
            }
        }

        Ok(PageDiffList {
            page_vaddr,
            page_size,
            chunks,
        })
    }

    /// Applies a `PageDiffList` patch into a target memory page in place.
    pub fn apply_patch(target_page: &mut [u8], diff: &PageDiffList) -> Result<(), DiffError> {
        let page_len = target_page.len();

        for chunk in &diff.chunks {
            let start = chunk.offset as usize;
            let end = start + chunk.bytes.len();

            if end > page_len {
                return Err(DiffError::OutOfBoundsPatch {
                    offset: start,
                    len: chunk.bytes.len(),
                    page_len,
                });
            }

            target_page[start..end].copy_from_slice(&chunk.bytes);
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compute_and_apply_diff() {
        let page_size = 4096;
        let baseline = vec![0u8; page_size];
        let mut dirty = baseline.clone();

        // Write modifications into dirty page
        dirty[100..105].copy_from_slice(&[1, 2, 3, 4, 5]);
        dirty[2000..2002].copy_from_slice(&[0xAA, 0xBB]);

        let diff = MemoryDiffEngine::compute_diff(0x1000, &dirty, &baseline)
            .expect("Diff computation should succeed");

        assert!(!diff.is_empty());
        assert_eq!(diff.chunks.len(), 2);
        assert_eq!(diff.chunks[0].offset, 100);
        assert_eq!(diff.chunks[0].bytes, vec![1, 2, 3, 4, 5]);
        assert_eq!(diff.chunks[1].offset, 2000);
        assert_eq!(diff.chunks[1].bytes, vec![0xAA, 0xBB]);

        // Patch baseline page and verify equality with dirty page
        let mut patch_target = baseline.clone();
        MemoryDiffEngine::apply_patch(&mut patch_target, &diff)
            .expect("Patch application should succeed");

        assert_eq!(patch_target, dirty);
    }

    #[test]
    fn test_no_diff() {
        let page = vec![0x42u8; 4096];
        let diff = MemoryDiffEngine::compute_diff(0x1000, &page, &page).unwrap();
        assert!(diff.is_empty());
        assert_eq!(diff.total_modified_bytes(), 0);
    }
}
