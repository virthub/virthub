// virthub/src/index/src/hash_index.rs

//! A concurrent, Robin Hood hash index for ultra‑low latency key lookups.
//!
//! This implementation uses a fixed‑capacity open‑addressing table with
//! Robin Hood hashing to minimize probe distances. The table is protected by
//! a single `parking_lot::RwLock` for thread safety – readers can proceed in
//! parallel while writers take exclusive access. The table does **not**
//! automatically resize; callers must choose an appropriate initial capacity.
//!
//! The `check_capacity` method is called before each insertion to enforce the
//! maximum load factor (`MAX_LOAD_FACTOR` = 0.75).

use store::kv_block::KvBlockKey;
use std::sync::Arc;
use parking_lot::RwLock;
use std::sync::atomic::{AtomicUsize, Ordering};
use thiserror::Error;

/// Default initial capacity for the hash index bucket array (must be a power of two).
pub const DEFAULT_INITIAL_CAPACITY: usize = 1024;

/// Maximum load factor threshold (0.75 = 75%) before the table is considered full.
pub const MAX_LOAD_FACTOR: f64 = 0.75;

#[derive(Debug, Error)]
pub enum HashIndexError {
    #[error("Key not found in hash index: {0}")]
    KeyNotFound(KvBlockKey),

    #[error("Hash index capacity exceeded max limit ({0})")]
    CapacityExceeded(usize),

    #[error("Index allocation error for capacity {capacity} with align {align}")]
    AllocationError { capacity: usize, align: usize },

    #[error("Attempted operation on an uninitialized or destroyed index")]
    IndexUninitialized,
}

/// Metadata stored in each hash index slot.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IndexSlotValue {
    /// Memory tier location tag (0 = DRAM, 1 = SSD, 2 = Remote Node).
    pub tier_id: u8,
    /// Physical block or frame offset.
    pub block_offset: u64,
    /// Byte length of payload.
    pub payload_len: u32,
}

/// Internal bucket layout.
#[derive(Debug, Clone, Copy)]
struct HashIndexBucket {
    key: KvBlockKey,
    value: IndexSlotValue,
    occupied: bool,
    probe_distance: u16,
}

impl HashIndexBucket {
    #[inline]
    fn empty() -> Self {
        Self {
            key: KvBlockKey::new(0, 0),
            value: IndexSlotValue {
                tier_id: 0,
                block_offset: 0,
                payload_len: 0,
            },
            occupied: false,
            probe_distance: 0,
        }
    }
}

/// A concurrent, Robin Hood Hash Index for ultra‑low latency key lookups.
///
/// The table uses a single `RwLock` to allow concurrent reads while writes are
/// serialised. This design is simpler and safer than the previous unsafe
/// lock‑free implementation, while retaining excellent performance for
/// read‑heavy workloads.
pub struct ConcurrentHashIndex {
    buckets: RwLock<Vec<HashIndexBucket>>,
    capacity: usize,
    mask: usize,
    count: AtomicUsize,
}

impl ConcurrentHashIndex {
    /// Creates a new `ConcurrentHashIndex` with a power‑of‑two capacity.
    pub fn new(capacity: usize) -> Result<Arc<Self>, HashIndexError> {
        let cap = capacity.next_power_of_two().max(16);
        let mask = cap - 1;

        // Pre‑initialise all buckets as empty.
        let mut buckets = Vec::with_capacity(cap);
        buckets.resize_with(cap, HashIndexBucket::empty);

        Ok(Arc::new(Self {
            buckets: RwLock::new(buckets),
            capacity: cap,
            mask,
            count: AtomicUsize::new(0),
        }))
    }

    /// Computes 64‑bit FNV‑1a hash value for a 128‑bit `KvBlockKey`.
    #[inline]
    fn hash_key(key: &KvBlockKey) -> usize {
        let mut hash: u64 = 0xcbf29ce484222325;
        let ns_bytes = key.namespace_id.to_le_bytes();
        let blk_bytes = key.block_id.to_le_bytes();

        for b in ns_bytes.iter().chain(blk_bytes.iter()) {
            hash ^= *b as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }

        hash as usize
    }

    /// Checks if the current load factor exceeds the maximum allowed.
    /// Returns `Ok(())` if under the limit, else `Err(CapacityExceeded)`.
    #[inline]
    fn check_capacity(&self) -> Result<(), HashIndexError> {
        if self.load_factor() > MAX_LOAD_FACTOR {
            return Err(HashIndexError::CapacityExceeded(self.capacity));
        }
        Ok(())
    }

    /// Inserts or updates a key in O(1) amortized time using Robin Hood linear probing.
    ///
    /// # Errors
    /// Returns `HashIndexError::CapacityExceeded` if the table is too full.
    pub fn insert(&self, key: KvBlockKey, value: IndexSlotValue) -> Result<(), HashIndexError> {
        // Check capacity before insertion.
        self.check_capacity()?;

        let mut write_guard = self.buckets.write();
        let buckets = &mut *write_guard;

        let mut curr_key = key;
        let mut curr_val = value;
        let mut curr_dist = 0u16;

        let start_idx = Self::hash_key(&curr_key) & self.mask;
        let mut idx = start_idx;

        loop {
            let bucket = &mut buckets[idx];

            if !bucket.occupied {
                bucket.key = curr_key;
                bucket.value = curr_val;
                bucket.occupied = true;
                bucket.probe_distance = curr_dist;
                self.count.fetch_add(1, Ordering::Relaxed);
                return Ok(());
            }

            if bucket.key == curr_key {
                // Key update.
                bucket.value = curr_val;
                return Ok(());
            }

            // Robin Hood displacement: swap if current item has traveled farther.
            if bucket.probe_distance < curr_dist {
                std::mem::swap(&mut bucket.key, &mut curr_key);
                std::mem::swap(&mut bucket.value, &mut curr_val);
                std::mem::swap(&mut bucket.probe_distance, &mut curr_dist);
            }

            idx = (idx + 1) & self.mask;
            curr_dist += 1;

            // Safety: prevent infinite loop if the table is full.
            if curr_dist > self.capacity as u16 {
                return Err(HashIndexError::CapacityExceeded(self.capacity));
            }
        }
    }

    /// Looks up a key and returns the associated `IndexSlotValue`.
    pub fn get(&self, key: &KvBlockKey) -> Result<IndexSlotValue, HashIndexError> {
        let read_guard = self.buckets.read();
        let buckets = &*read_guard;

        let start_idx = Self::hash_key(key) & self.mask;
        let mut idx = start_idx;
        let mut dist = 0u16;

        loop {
            let bucket = &buckets[idx];

            if !bucket.occupied || dist > bucket.probe_distance {
                return Err(HashIndexError::KeyNotFound(*key));
            }

            if bucket.key == *key {
                return Ok(bucket.value);
            }

            idx = (idx + 1) & self.mask;
            dist += 1;

            if dist > self.capacity as u16 {
                return Err(HashIndexError::KeyNotFound(*key));
            }
        }
    }

    /// Removes a key from the index and performs backward‑shift deletion to preserve Robin Hood invariants.
    pub fn remove(&self, key: &KvBlockKey) -> Result<IndexSlotValue, HashIndexError> {
        let mut write_guard = self.buckets.write();
        let buckets = &mut *write_guard;

        let start_idx = Self::hash_key(key) & self.mask;
        let mut idx = start_idx;
        let mut dist = 0u16;

        loop {
            let bucket = &mut buckets[idx];

            if !bucket.occupied || dist > bucket.probe_distance {
                return Err(HashIndexError::KeyNotFound(*key));
            }

            if bucket.key == *key {
                let removed_val = bucket.value;

                // Backward shift deletion loop.
                let mut prev_idx = idx;
                let mut curr_idx = (idx + 1) & self.mask;

                loop {
                    let next_bucket = &mut buckets[curr_idx];

                    if !next_bucket.occupied || next_bucket.probe_distance == 0 {
                        buckets[prev_idx] = HashIndexBucket::empty();
                        break;
                    }

                    next_bucket.probe_distance -= 1;
                    buckets[prev_idx] = *next_bucket;

                    prev_idx = curr_idx;
                    curr_idx = (curr_idx + 1) & self.mask;
                }

                self.count.fetch_sub(1, Ordering::Relaxed);
                return Ok(removed_val);
            }

            idx = (idx + 1) & self.mask;
            dist += 1;
        }
    }

    /// Returns the total number of occupied entries in the index.
    pub fn len(&self) -> usize {
        self.count.load(Ordering::Relaxed)
    }

    /// Returns whether the index is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns current capacity.
    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Returns current load factor (len / capacity).
    pub fn load_factor(&self) -> f64 {
        self.len() as f64 / self.capacity as f64
    }

    /// Returns an iterator over all key‑value pairs.
    pub fn iter(&self) -> impl Iterator<Item = (KvBlockKey, IndexSlotValue)> + '_ {
        let read_guard = self.buckets.read();
        read_guard
            .iter()
            .filter(|b| b.occupied)
            .map(|b| (b.key, b.value))
            .collect::<Vec<_>>()
            .into_iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_and_get() {
        let index = ConcurrentHashIndex::new(64).expect("Index creation should succeed");
        let key = KvBlockKey::new(1, 100);
        let val = IndexSlotValue {
            tier_id: 0,
            block_offset: 4096,
            payload_len: 2048,
        };

        index.insert(key, val).expect("Insert should succeed");
        assert_eq!(index.len(), 1);

        let retrieved = index.get(&key).expect("Get should succeed");
        assert_eq!(retrieved, val);
    }

    #[test]
    fn test_update_existing_key() {
        let index = ConcurrentHashIndex::new(64).unwrap();
        let key = KvBlockKey::new(1, 200);

        let val1 = IndexSlotValue { tier_id: 0, block_offset: 0, payload_len: 1024 };
        let val2 = IndexSlotValue { tier_id: 1, block_offset: 8192, payload_len: 1024 };

        index.insert(key, val1).unwrap();
        index.insert(key, val2).unwrap();

        assert_eq!(index.len(), 1);
        let retrieved = index.get(&key).unwrap();
        assert_eq!(retrieved.tier_id, 1);
        assert_eq!(retrieved.block_offset, 8192);
    }

    #[test]
    fn test_remove_with_backward_shift() {
        let index = ConcurrentHashIndex::new(64).unwrap();
        let k1 = KvBlockKey::new(1, 101);
        let k2 = KvBlockKey::new(1, 102);

        let v1 = IndexSlotValue { tier_id: 0, block_offset: 1000, payload_len: 512 };
        let v2 = IndexSlotValue { tier_id: 0, block_offset: 2000, payload_len: 512 };

        index.insert(k1, v1).unwrap();
        index.insert(k2, v2).unwrap();
        assert_eq!(index.len(), 2);

        let removed = index.remove(&k1).expect("Remove should succeed");
        assert_eq!(removed, v1);
        assert_eq!(index.len(), 1);

        // Verify k2 remains accessible after backward shift.
        let k2_val = index.get(&k2).expect("k2 must still be present");
        assert_eq!(k2_val, v2);
    }

    #[test]
    fn test_capacity_exceeded() {
        // For capacity 16, max entries before exceeding is 12 (75% load).
        let index = ConcurrentHashIndex::new(16).unwrap();
        for i in 0..12 {
            let key = KvBlockKey::new(1, i);
            let val = IndexSlotValue { tier_id: 0, block_offset: i * 4096, payload_len: 512 };
            index.insert(key, val).unwrap();
        }
        // 13th insertion should be allowed (load before = 12/16 = 0.75, not >)
        let key13 = KvBlockKey::new(1, 12);
        let val13 = IndexSlotValue { tier_id: 0, block_offset: 12 * 4096, payload_len: 512 };
        assert!(index.insert(key13, val13).is_ok());

        // 14th should fail (load before = 13/16 = 0.8125 > 0.75)
        let key14 = KvBlockKey::new(1, 13);
        let val14 = IndexSlotValue { tier_id: 0, block_offset: 13 * 4096, payload_len: 512 };
        let err = index.insert(key14, val14);
        assert!(matches!(err, Err(HashIndexError::CapacityExceeded(16))));
    }

    #[test]
    fn test_iter() {
        let index = ConcurrentHashIndex::new(64).unwrap();
        let keys = [KvBlockKey::new(1, 1), KvBlockKey::new(1, 2), KvBlockKey::new(2, 1)];
        for (i, k) in keys.iter().enumerate() {
            index.insert(*k, IndexSlotValue { tier_id: 0, block_offset: i as u64, payload_len: 10 }).unwrap();
        }
        let collected: Vec<_> = index.iter().collect();
        assert_eq!(collected.len(), 3);
    }
}
