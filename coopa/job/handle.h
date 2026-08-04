/**
 * @file handle.h
 * @brief Defines the JobHandle class for tracking asynchronous job completion.
 *
 * JobHandle is a lightweight, trivially-copyable token that references
 * an atomic counter in an engine-owned pool. It avoids all heap allocations
 * by storing only a 32-bit index into the counter pool.
 */

#ifndef JOB_HANDLE_H
#define JOB_HANDLE_H

#include <atomic>
#include <cstdint>

namespace coopa {
namespace job {

/// @brief Sentinel value representing an uninitialized or invalid handle.
inline constexpr uint32_t k_invalid_handle_index = UINT32_MAX;

/**
 * @class CounterPool
 * @brief Pre-allocated pool of atomic counters for zero-allocation handle tracking.
 *
 * Manages a fixed-capacity array of atomic<int32_t> counters. Each counter
 * tracks the number of outstanding jobs associated with a handle. Counters are
 * allocated via a lock-free bump allocator and recycled per-frame via reset().
 */
class CounterPool {
public:
    /**
     * @brief Constructs a CounterPool with the given capacity.
     * @param capacity Maximum number of counters available per frame.
     */
    explicit CounterPool(uint32_t capacity)
        : capacity_(capacity),
          next_index_(0)
    {
        counters_ = new std::atomic<int32_t>[capacity_];
        for (uint32_t i = 0; i < capacity_; ++i) {
            counters_[i].store(0, std::memory_order_relaxed);
        }
    }

    /// @brief Destructor. Frees the counter array.
    ~CounterPool() {
        delete[] counters_;
    }

    /// @brief Non-copyable.
    CounterPool(const CounterPool&) = delete;

    /// @brief Non-copyable.
    CounterPool& operator=(const CounterPool&) = delete;

    /**
     * @brief Allocates the next available counter index.
     *
     * Uses an atomic fetch_add for lock-free allocation. The allocated counter
     * is initialized to zero before being returned.
     *
     * @return The index of the newly allocated counter.
     * @note Returns k_invalid_handle_index if the pool is exhausted.
     */
    uint32_t allocate() {
        uint32_t idx = next_index_.fetch_add(1, std::memory_order_relaxed);
        if (idx >= capacity_) {
            // Pool exhausted — revert and return invalid.
            next_index_.fetch_sub(1, std::memory_order_relaxed);
            return k_invalid_handle_index;
        }
        counters_[idx].store(0, std::memory_order_relaxed);
        return idx;
    }

    /**
     * @brief Returns a pointer to the atomic counter at the given index.
     * @param index The counter index.
     * @return Pointer to the counter, or nullptr if index is invalid.
     */
    std::atomic<int32_t>* get(uint32_t index) const {
        if (index >= capacity_ || index == k_invalid_handle_index) {
            return nullptr;
        }
        return &counters_[index];
    }

    /**
     * @brief Resets the pool's bump allocator, making all slots reusable.
     *
     * This must only be called when all previously-issued handles are known to
     * be complete (e.g., at a frame boundary). It does NOT zero the counters —
     * each allocate() call initializes its own slot.
     */
    void reset() {
        next_index_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Returns the total capacity of the pool.
     * @return Number of counter slots.
     */
    uint32_t capacity() const { return capacity_; }

    /**
     * @brief Returns how many counters have been allocated since the last reset.
     * @return Current allocation high-water mark.
     */
    uint32_t allocated_count() const {
        return next_index_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int32_t>* counters_;  /**< Raw array of atomic counters. */
    uint32_t capacity_;               /**< Maximum number of counters. */
    std::atomic<uint32_t> next_index_; /**< Next allocation index (bump allocator). */
};

/**
 * @class JobHandle
 * @brief Lightweight token for tracking asynchronous job completion.
 *
 * A JobHandle is a trivially-copyable 8-byte value containing a pool pointer
 * and a 32-bit index. It replaces the previous shared_ptr-based design to
 * eliminate heap allocations, reference counting overhead, and cache-line
 * bouncing on copies.
 *
 * A handle is "valid" when its index is not k_invalid_handle_index and its
 * pool pointer is non-null. An invalid handle always reports as complete.
 */
class JobHandle {
public:
    /**
     * @brief Default constructor. Creates an invalid (always-complete) handle.
     */
    JobHandle()
        : pool_(nullptr),
          index_(k_invalid_handle_index)
    {}

    /**
     * @brief Constructs a JobHandle referencing a specific counter in a pool.
     * @param pool Pointer to the owning CounterPool.
     * @param index The counter index within the pool.
     */
    JobHandle(CounterPool* pool, uint32_t index)
        : pool_(pool),
          index_(index)
    {}

    /// @brief Default destructor. Trivially destructible — no cleanup needed.
    ~JobHandle() = default;

    /// @brief Default copy constructor. Trivially copyable.
    JobHandle(const JobHandle&) = default;

    /// @brief Default copy assignment. Trivially copyable.
    JobHandle& operator=(const JobHandle&) = default;

    /**
     * @brief Checks if all jobs contributing to this handle have finished.
     * @return True if the counter has reached 0 or the handle is invalid.
     */
    bool is_complete() const {
        if (!is_valid()) return true;
        return pool_->get(index_)->load(std::memory_order_acquire) == 0;
    }

    /**
     * @brief Checks if this handle references a valid counter.
     * @return True if the handle has a valid pool and index.
     */
    bool is_valid() const {
        return pool_ != nullptr && index_ != k_invalid_handle_index;
    }

    /**
     * @brief Retrieves a pointer to the underlying atomic counter.
     * @return Pointer to the counter, or nullptr if the handle is invalid.
     */
    std::atomic<int32_t>* get_counter() const {
        if (!is_valid()) return nullptr;
        return pool_->get(index_);
    }

    /**
     * @brief Returns the raw counter index for identity comparisons and dedup.
     * @return The index, or k_invalid_handle_index if invalid.
     */
    uint32_t index() const { return index_; }

    /**
     * @brief Equality comparison based on counter index identity.
     * @param other The other handle to compare.
     * @return True if both handles reference the same counter slot.
     */
    bool operator==(const JobHandle& other) const {
        return pool_ == other.pool_ && index_ == other.index_;
    }

    /**
     * @brief Inequality comparison.
     * @param other The other handle to compare.
     * @return True if the handles reference different counter slots.
     */
    bool operator!=(const JobHandle& other) const {
        return !(*this == other);
    }

    /**
     * @brief Less-than comparison for sorting (by index).
     * @param other The other handle to compare.
     * @return True if this handle's index is less than the other's.
     */
    bool operator<(const JobHandle& other) const {
        return index_ < other.index_;
    }

private:
    CounterPool* pool_; /**< Pointer to the owning counter pool (non-owning). */
    uint32_t index_;    /**< Index into the counter pool. */
};

} // namespace job
} // namespace coopa

#endif // JOB_HANDLE_H