// virthub/src/store/src/kv_block.rs

use bincode;
use serde::{Deserialize, Serialize};
use std::alloc::{alloc, dealloc, Layout};
use std::fmt;
use std::ptr::NonNull;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use thiserror::Error;

use precision::PackedBlockPolicy;
use crate::psp_kv::PspKvSidecarDescriptor;

/// Standard block size allocation boundary (default: 4096 bytes / 4KB aligned)
pub const DEFAULT_BLOCK_SIZE: usize = 4096;

/// Hardware cache-line alignment boundary
pub const CACHE_LINE_ALIGN: usize = 64;

/// Errors originating from KV block allocation, serialization, or validation operations.
#[derive(Debug, Error)]
pub enum KvBlockError {
    #[error("Block allocation failed for size {size} bytes with alignment {align}")]
    AllocationFailed { size: usize, align: usize },

    #[error("Block boundary offset ({offset}) with length ({len}) exceeds capacity ({capacity})")]
    OutOfBounds {
        offset: usize,
        len: usize,
        capacity: usize,
    },

    #[error("Checksum mismatch: expected 0x{expected:x}, computed 0x{computed:x}")]
    ChecksumMismatch { expected: u32, computed: u32 },

    #[error("Block serialization/deserialization error: {0}")]
    SerializationFailed(#[from] bincode::Error),

    #[error("Attempted to operate on a freed or uninitialized KV block pointer")]
    InvalidBlockPointer,
}

/// Unique 128-bit key identifying a KV block in index and persistent store.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct KvBlockKey {
    pub namespace_id: u64,
    pub block_id: u64,
}

impl KvBlockKey {
    pub fn new(namespace_id: u64, block_id: u64) -> Self {
        Self {
            namespace_id,
            block_id,
        }
    }
}

impl fmt::Display for KvBlockKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "BlockKey(NS:{}, ID:{})", self.namespace_id, self.block_id)
    }
}

/// Header metadata stored alongside raw byte payloads inside KV blocks.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct KvBlockHeader {
    pub key: KvBlockKey,
    pub payload_len: u32,
    pub checksum: u32,
    pub flags: u32,
}

/// A heap-allocated, aligned memory block optimized for zero-copy I/O.
pub struct RawBlockBuffer {
    ptr: NonNull<u8>,
    layout: Layout,
}

impl RawBlockBuffer {
    /// Allocates an aligned memory block buffer of given size and alignment.
    pub fn allocate(size: usize, align: usize) -> Result<Self, KvBlockError> {
        // Validate alignment is power of two
        if !align.is_power_of_two() {
            return Err(KvBlockError::AllocationFailed { size, align });
        }
        if size == 0 {
            return Err(KvBlockError::AllocationFailed { size, align });
        }

        let layout = Layout::from_size_align(size, align)
            .map_err(|_| KvBlockError::AllocationFailed { size, align })?;

        let raw_ptr = unsafe { alloc(layout) };
        let ptr = NonNull::new(raw_ptr)
            .ok_or(KvBlockError::AllocationFailed { size, align })?;

        // Zero the memory for safety
        unsafe {
            std::ptr::write_bytes(ptr.as_ptr(), 0, size);
        }

        Ok(Self { ptr, layout })
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.ptr.as_ptr(), self.layout.size()) }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut(self.ptr.as_ptr(), self.layout.size()) }
    }

    pub fn capacity(&self) -> usize {
        self.layout.size()
    }

    pub fn aligned_ptr(&self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    pub fn alignment(&self) -> usize {
        self.layout.align()
    }
}

impl Drop for RawBlockBuffer {
    fn drop(&mut self) {
        unsafe {
            dealloc(self.ptr.as_ptr(), self.layout);
        }
    }
}

unsafe impl Send for RawBlockBuffer {}
unsafe impl Sync for RawBlockBuffer {}

/// High-performance Key-Value block representation holding payload data and checksums.
///
/// In addition to the header and payload, this struct now carries optional
/// PSP‑KV metadata: a packed precision policy and a sidecar descriptor.
/// These fields are used by the upper‑layer precision management and are
/// serialized together with the block when transmitted over the network.
#[derive(Clone)]
pub struct KvBlock {
    header: KvBlockHeader,
    payload: Arc<Vec<u8>>,
    ref_count: Arc<AtomicU32>,
    /// Packed precision policy (optional). Contains precision level,
    /// residual flag, and active head mask.
    precision_policy: Option<PackedBlockPolicy>,
    /// Sidecar descriptor (optional). Contains pointers, scales, and
    /// head presence masks for quantized formats.
    sidecar: Option<PspKvSidecarDescriptor>,
}

impl KvBlock {
    /// Instantiates a new KV Block with computed CRC32 checksum over the payload.
    /// The precision policy and sidecar are initialized to `None`.
    pub fn new(key: KvBlockKey, payload: Vec<u8>) -> Self {
        let checksum = crc32fast::hash(&payload);
        let payload_len = payload.len() as u32;

        let header = KvBlockHeader {
            key,
            payload_len,
            checksum,
            flags: 0,
        };

        Self {
            header,
            payload: Arc::new(payload),
            ref_count: Arc::new(AtomicU32::new(1)),
            precision_policy: None,
            sidecar: None,
        }
    }

    /// Instantiates a new KV Block with the given precision policy and sidecar.
    pub fn new_with_policy(
        key: KvBlockKey,
        payload: Vec<u8>,
        precision_policy: Option<PackedBlockPolicy>,
        sidecar: Option<PspKvSidecarDescriptor>,
    ) -> Self {
        let mut block = Self::new(key, payload);
        block.precision_policy = precision_policy;
        block.sidecar = sidecar;
        block
    }

    /// Verifies block payload integrity against header CRC32 checksum.
    pub fn verify_checksum(&self) -> Result<(), KvBlockError> {
        let computed = crc32fast::hash(&self.payload);
        if computed != self.header.checksum {
            return Err(KvBlockError::ChecksumMismatch {
                expected: self.header.checksum,
                computed,
            });
        }
        Ok(())
    }

    /// Returns the block key descriptor.
    pub fn key(&self) -> KvBlockKey {
        self.header.key
    }

    /// Returns reference to block header metadata.
    pub fn header(&self) -> &KvBlockHeader {
        &self.header
    }

    /// Returns slice reference to raw payload.
    pub fn payload(&self) -> &[u8] {
        &self.payload
    }

    /// Returns mutable slice reference to payload, resetting checksum validation state.
    ///
    /// This will clone the payload if the `Arc` is shared, but avoids copying
    /// when the block is uniquely owned.
    pub fn payload_mut(&mut self) -> &mut [u8] {
        Arc::make_mut(&mut self.payload).as_mut_slice()
    }

    /// Recalculates and updates internal CRC32 checksum after mutating payload.
    pub fn update_checksum(&mut self) {
        self.header.checksum = crc32fast::hash(&self.payload);
        self.header.payload_len = self.payload.len() as u32;
    }

    /// Returns the optional packed precision policy.
    pub fn precision_policy(&self) -> Option<PackedBlockPolicy> {
        self.precision_policy
    }

    /// Sets the packed precision policy.
    pub fn set_precision_policy(&mut self, policy: Option<PackedBlockPolicy>) {
        self.precision_policy = policy;
    }

    /// Returns the optional PSP‑KV sidecar descriptor.
    pub fn sidecar(&self) -> Option<&PspKvSidecarDescriptor> {
        self.sidecar.as_ref()
    }

    /// Sets the PSP‑KV sidecar descriptor.
    pub fn set_sidecar(&mut self, sidecar: Option<PspKvSidecarDescriptor>) {
        self.sidecar = sidecar;
    }

    /// Retains reference counter.
    pub fn retain(&self) {
        self.ref_count.fetch_add(1, Ordering::Relaxed);
    }

    /// Releases reference counter.
    pub fn release(&self) -> u32 {
        self.ref_count.fetch_sub(1, Ordering::Release)
    }

    /// Serializes entire block (header + payload + optional metadata) into a byte vector.
    pub fn serialize(&self) -> Result<Vec<u8>, KvBlockError> {
        let bytes = bincode::serialize(&(
            &self.header,
            &*self.payload,
            self.precision_policy,
            self.sidecar,
        ))?;
        Ok(bytes)
    }

    /// Deserializes a block from a byte slice and verifies checksum integrity.
    pub fn deserialize(bytes: &[u8]) -> Result<Self, KvBlockError> {
        let (header, payload, precision_policy, sidecar):
            (KvBlockHeader, Vec<u8>, Option<PackedBlockPolicy>, Option<PspKvSidecarDescriptor>) =
            bincode::deserialize(bytes)?;
        let block = Self {
            header,
            payload: Arc::new(payload),
            ref_count: Arc::new(AtomicU32::new(1)),
            precision_policy,
            sidecar,
        };

        block.verify_checksum()?;
        Ok(block)
    }

    /// Batch registration of multiple blocks from serialized data (optimization).
    pub fn deserialize_batch(data: &[u8]) -> Result<Vec<Self>, KvBlockError> {
        let blocks: Vec<(KvBlockHeader, Vec<u8>, Option<PackedBlockPolicy>, Option<PspKvSidecarDescriptor>)> =
            bincode::deserialize(data)?;
        let mut result = Vec::with_capacity(blocks.len());
        for (header, payload, precision_policy, sidecar) in blocks {
            let block = Self {
                header,
                payload: Arc::new(payload),
                ref_count: Arc::new(AtomicU32::new(1)),
                precision_policy,
                sidecar,
            };
            block.verify_checksum()?;
            result.push(block);
        }
        Ok(result)
    }
}

impl fmt::Debug for KvBlock {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("KvBlock")
            .field("header", &self.header)
            .field("payload_len", &self.payload.len())
            .field("ref_count", &self.ref_count.load(Ordering::Relaxed))
            .field("precision_policy", &self.precision_policy)
            .field("sidecar", &self.sidecar)
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::psp_kv::PspKvSidecarDescriptor;
    use precision::{PackedBlockPolicy, PrecisionLevel};

    #[test]
    fn test_kv_block_checksum_verification() {
        let key = KvBlockKey::new(1, 100);
        let payload = vec![1, 2, 3, 4, 5, 6, 7, 8];

        let block = KvBlock::new(key, payload);
        assert!(block.verify_checksum().is_ok());

        // Corrupt payload copy and check verification failure
        let mut corrupted = block.clone();
        corrupted.payload_mut()[0] ^= 0xFF;
        assert!(matches!(
            corrupted.verify_checksum(),
            Err(KvBlockError::ChecksumMismatch { .. })
        ));
    }

    #[test]
    fn test_kv_block_serialization_roundtrip_with_metadata() {
        let key = KvBlockKey::new(42, 1001);
        let payload = vec![0xAB; 512];
        let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8, false, 0xFFFFFF);
        let sidecar = PspKvSidecarDescriptor::default();

        let block = KvBlock::new_with_policy(key, payload.clone(), Some(policy), Some(sidecar));
        let serialized = block.serialize().expect("Serialization should succeed");

        let decoded = KvBlock::deserialize(&serialized).expect("Deserialization should succeed");
        assert_eq!(decoded.key(), key);
        assert_eq!(decoded.payload(), payload.as_slice());
        assert_eq!(decoded.precision_policy(), Some(policy));
        assert_eq!(decoded.sidecar(), Some(&sidecar));
    }

    #[test]
    fn test_raw_block_buffer_aligned_allocation() {
        let mut buffer = RawBlockBuffer::allocate(4096, CACHE_LINE_ALIGN)
            .expect("Allocation should succeed");

        assert_eq!(buffer.capacity(), 4096);
        let slice = buffer.as_mut_slice();
        slice[0] = 0xFE;
        slice[4095] = 0xEF;

        assert_eq!(buffer.as_slice()[0], 0xFE);
        assert_eq!(buffer.as_slice()[4095], 0xEF);
    }

    #[test]
    fn test_clone_shares_payload() {
        let key = KvBlockKey::new(1, 1);
        let block = KvBlock::new(key, vec![0; 1024]);
        let clone = block.clone();
        assert!(Arc::ptr_eq(&block.payload, &clone.payload));
    }

    #[test]
    fn test_mutation_after_clone_is_copy_on_write() {
        let key = KvBlockKey::new(2, 2);
        let original = KvBlock::new(key, vec![0; 10]);
        let mut clone = original.clone();
        clone.payload_mut()[0] = 1;
        assert_eq!(original.payload()[0], 0);
        assert_eq!(clone.payload()[0], 1);
    }
}
