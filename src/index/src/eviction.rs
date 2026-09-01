// virthub/src/index/src/eviction.rs

use std::collections::VecDeque;
use parking_lot::Mutex;
use store::kv_block::KvBlockKey;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum EvictionError {
    #[error("Eviction queue is empty")]
    QueueEmpty,

    #[error("Target Key not found in eviction index")]
    KeyNotFound,
}

/// Abstract trait for cache eviction strategies across Virthub storage tiers
pub trait EvictionPolicy: Send + Sync {
    fn record_access(&self, key: &KvBlockKey);
    fn evict(&self) -> Result<KvBlockKey, EvictionError>;
}

/// 2Q (Two-Queue) Cache Eviction Policy for handling frequency and recency queues.
///
/// This implementation uses `parking_lot::Mutex` for low‑overhead locking.
/// All methods are `async` for API compatibility, but internally they lock
/// synchronously and do not yield, which is acceptable for short critical sections.
pub struct TwoQEvictionPolicy {
    in_queue: Mutex<VecDeque<KvBlockKey>>,
    lru_queue: Mutex<VecDeque<KvBlockKey>>,
}

impl TwoQEvictionPolicy {
    pub fn new() -> Self {
        Self {
            in_queue: Mutex::new(VecDeque::new()),
            lru_queue: Mutex::new(VecDeque::new()),
        }
    }

    /// Update page access status; promotes keys between FIFO and LRU queues
    pub async fn touch(&self, key: &KvBlockKey) {
        // Lock LRU queue first
        let mut lru = self.lru_queue.lock();
        if let Some(pos) = lru.iter().position(|k| k == key) {
            let k = lru.remove(pos).unwrap();
            lru.push_back(k);
            return;
        }
        drop(lru);

        // Not in LRU, check in-queue
        let mut in_q = self.in_queue.lock();
        if let Some(pos) = in_q.iter().position(|k| k == key) {
            let k = in_q.remove(pos).unwrap();
            in_q.push_back(k);
        } else {
            in_q.push_back(*key);
        }
    }

    /// Select and evict the next candidate key based on 2Q semantics
    pub async fn evict_next(&self) -> Result<KvBlockKey, EvictionError> {
        // First try LRU queue
        let mut lru = self.lru_queue.lock();
        if let Some(key) = lru.pop_front() {
            return Ok(key);
        }
        drop(lru);

        // Then in-queue
        let mut in_q = self.in_queue.lock();
        in_q.pop_front().ok_or(EvictionError::QueueEmpty)
    }

    /// Remove a key explicitly upon manual invalidation or block deletion
    pub async fn remove(&self, key: &KvBlockKey) -> Result<(), EvictionError> {
        // Check LRU first
        let mut lru = self.lru_queue.lock();
        if let Some(pos) = lru.iter().position(|k| k == key) {
            lru.remove(pos);
            return Ok(());
        }
        drop(lru);

        // Then in-queue
        let mut in_q = self.in_queue.lock();
        if let Some(pos) = in_q.iter().position(|k| k == key) {
            in_q.remove(pos);
            return Ok(());
        }

        Err(EvictionError::KeyNotFound)
    }
}

impl Default for TwoQEvictionPolicy {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_two_q_eviction_lifecycle() {
        let policy = TwoQEvictionPolicy::new();
        let key1 = KvBlockKey::new(1, 100);
        let key2 = KvBlockKey::new(1, 101);

        policy.touch(&key1).await;
        policy.touch(&key2).await;

        let evicted = policy.evict_next().await.expect("Key should be evicted");
        assert_eq!(evicted, key1);

        let evicted_second = policy.evict_next().await.expect("Key should be evicted");
        assert_eq!(evicted_second, key2);

        assert!(policy.evict_next().await.is_err());
    }
}
