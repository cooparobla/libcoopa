/**
 * @file work_stealing_deque.h
 * @brief Lock-free Chase-Lev work-stealing deque for the job system.
 *
 * Implements the Chase-Lev dynamic circular work-stealing deque as described
 * in "Dynamic Circular Work-Stealing Deque" (Chase & Lev, 2005). The owner
 * thread pushes/pops from the bottom (no contention), while thieves steal
 * from the top using a single CAS operation.
 *
 * This implementation uses a fixed-capacity circular buffer (no dynamic
 * resizing) for deterministic memory behavior in realtime applications.
 */

#ifndef COOPA_JOB_WORK_STEALING_DEQUE_H
#define COOPA_JOB_WORK_STEALING_DEQUE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility> // For std::move

#include <coopa/job/platform.h>

namespace coopa {
namespace job {

/**
 * @class WorkStealingDeque
 * @brief Lock-free single-producer multi-consumer work-stealing deque.
 *
 * @tparam T The element type. Must be move-constructible.
 *
 * Thread safety guarantees:
 * - push() and pop() may only be called by the **owner** thread.
 * - steal() may be called by **any** thread concurrently.
 * - push/pop and steal operate on opposite ends of the deque, minimizing contention.
 *
 * Memory ordering follows the Chase-Lev paper:
 * - bottom_ is only written by the owner (relaxed loads/stores from owner, acquire from thieves).
 * - top_ is contended between owner pop() and thief steal() (CAS with acq_rel).
 */
template<typename T>
class WorkStealingDeque {
public:
    /// @brief Constructs a deque with the given fixed capacity (must be a power of 2).
    explicit WorkStealingDeque(std::size_t capacity = 1024)
        : capacity_(capacity),
          mask_(capacity - 1),
          buffer_(nullptr),
          bottom_(0),
          top_(0)
    {
        // Capacity must be a power of 2 for bitmask indexing.
        // If not, round up to the next power of 2.
        if ((capacity & (capacity - 1)) != 0) {
            std::size_t v = capacity;
            v--;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            v |= v >> 32;
            v++;
            capacity_ = v;
            mask_ = v - 1;
        }

        buffer_ = new Slot[capacity_];
    }

    /// @brief Destructor. Frees the buffer.
    ~WorkStealingDeque() {
        delete[] buffer_;
    }

    /// @brief Non-copyable.
    WorkStealingDeque(const WorkStealingDeque&) = delete;

    /// @brief Non-copyable.
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    /**
     * @brief Pushes an item onto the bottom of the deque (owner thread only).
     *
     * @param item The item to push (moved into the deque).
     * @return True if the item was successfully pushed, false if the deque is full.
     */
    bool push(T&& item) {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_acquire);

        // Check capacity.
        if (static_cast<std::size_t>(b - t) >= capacity_) {
            return false; // Full — fixed capacity, no resize.
        }

        buffer_[b & mask_].data = std::move(item);

        // Ensure the item is written before bottom_ is incremented.
        // Release fence ensures thieves see the data.
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);

        return true;
    }

    /**
     * @brief Pops an item from the bottom of the deque (owner thread only).
     *
     * @param out_item The item popped (if successful).
     * @return True if an item was popped, false if the deque was empty.
     */
    bool pop(T& out_item) {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);

        // Full fence to ensure the decrement of bottom_ is visible to thieves
        // before we read top_.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        int64_t t = top_.load(std::memory_order_relaxed);

        if (t < b) {
            // Non-empty, and not the last element. A concurrent steal() only
            // ever targets buffer_[top_ & mask_], which is a different slot
            // than buffer_[b & mask_] here, so moving out immediately is safe.
            out_item = std::move(buffer_[b & mask_].data);
            return true;
        } else if (t == b) {
            // Exactly one element left — a concurrent steal() may be racing
            // for this same slot. Resolve ownership via CAS BEFORE touching
            // buffer_[b & mask_]: moving out first (as this used to do) races
            // a thief's own move-out of the identical slot whenever the CAS
            // was going to lose anyway — for a non-trivial T (e.g. Job, whose
            // TaskWrapper move nulls out the source's callable) that is a
            // genuine data race that can silently leave one side holding a
            // corrupted, no-op value while still reporting success.
            bool won = top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed);
            bottom_.store(t + 1, std::memory_order_relaxed);
            if (!won) {
                // Lost the race — thief took it. Do not touch the slot.
                return false;
            }
            out_item = std::move(buffer_[b & mask_].data);
            return true;
        } else {
            // Empty.
            bottom_.store(t, std::memory_order_relaxed);
            return false;
        }
    }

    /**
     * @brief Steals an item from the top of the deque (any thread).
     *
     * @param out_item The item stolen (if successful).
     * @return True if an item was stolen, false if the deque was empty or contended.
     */
    bool steal(T& out_item) {
        int64_t t = top_.load(std::memory_order_acquire);

        // Acquire fence to ensure we see the data written by push().
        std::atomic_thread_fence(std::memory_order_seq_cst);

        int64_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            // Non-empty. Resolve ownership of this slot via CAS BEFORE
            // touching buffer_[t & mask_]: another thief racing for the same
            // slot (or the owner's pop(), see its own doc) is doing the
            // identical check. Moving the data out first — as this used to —
            // means every loser has already performed an unsynchronized
            // move-read of the same slot the winner is also reading, which
            // is a genuine data race for a non-trivial T (Job's TaskWrapper
            // move nulls out the source, so a losing thief could silently
            // hand back a corrupted, no-op job while still reporting the
            // winner's slot as claimed).
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                // Lost the race to another thief or to pop() — do not touch the slot.
                return false;
            }
            out_item = std::move(buffer_[t & mask_].data);
            return true;
        }

        return false; // Empty.
    }

    /**
     * @brief Returns the approximate number of items in the deque.
     *
     * This is a best-effort snapshot — the actual count may change immediately
     * after this call returns.
     *
     * @return Approximate item count.
     */
    std::size_t size_approx() const {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        int64_t t = top_.load(std::memory_order_relaxed);
        return static_cast<std::size_t>(b >= t ? b - t : 0);
    }

    /**
     * @brief Checks if the deque is approximately empty.
     * @return True if no items are present (best-effort).
     */
    bool empty_approx() const {
        return size_approx() == 0;
    }

    /**
     * @brief Resets the deque to empty state. Owner thread only, not thread-safe.
     */
    void clear() {
        bottom_.store(0, std::memory_order_relaxed);
        top_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Returns the fixed capacity of the deque.
     * @return Capacity in number of elements.
     */
    std::size_t capacity() const { return capacity_; }

private:
    /**
     * @struct Slot
     * @brief Cache-line-padded storage slot to prevent false sharing between
     *        adjacent elements accessed by different threads.
     */
    struct Slot {
        T data;
    };

    std::size_t capacity_; /**< Fixed capacity (power of 2). */
    std::size_t mask_;     /**< Bitmask for circular indexing (capacity_ - 1). */
    Slot* buffer_;         /**< Dynamically allocated circular buffer. */

    /// @brief Bottom index (owner-side). Padded to its own cache line.
    alignas(k_cache_line_size) std::atomic<int64_t> bottom_;

    /// @brief Top index (thief-side). Padded to its own cache line.
    alignas(k_cache_line_size) std::atomic<int64_t> top_;
};

} // namespace job
} // namespace coopa

#endif // COOPA_JOB_WORK_STEALING_DEQUE_H
