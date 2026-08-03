/**
 * @file job.h
 * @brief Defines the Job structure representing a task in the work-stealing job system.
 *
 * Optimized for realtime/game-engine use: uses inline fixed-capacity dependency
 * storage and a function pointer + userdata callable instead of std::function
 * to eliminate heap allocations in the hot path.
 */

#ifndef JOB_H
#define JOB_H

#include <cstdint>
#include <cstring>    // For std::memcpy
#include <functional> // For std::function (used in TaskWrapper)
#include <new>        // For placement new
#include <type_traits>
#include <utility>    // For std::move

#include <coopa/job/handle.h>
#include <coopa/job/platform.h>

/// @brief Categorization tag for jobs (used for dedication and metrics).
using JobType = uint32_t;

namespace trav {
namespace job {

/**
 * @class TaskWrapper
 * @brief A small-buffer-optimized callable that avoids heap allocation for
 *        callables up to k_task_buffer_size bytes.
 *
 * Uses placement new into an inline buffer. For callables that exceed the
 * buffer size, falls back to std::function<void()> stored in the buffer
 * (which may itself heap-allocate, but this path is rare in practice).
 *
 * The buffer size is tuned so that common lambda captures (a few pointers
 * and small values) fit without spilling to the heap.
 */
class TaskWrapper {
    /// @brief Size of the inline callable storage buffer in bytes.
    static constexpr std::size_t k_task_buffer_size = 48;

    /// @brief Type-erased function pointer for invoking the stored callable.
    using InvokeFn = void(*)(void*);

    /// @brief Type-erased function pointer for destroying the stored callable.
    using DestroyFn = void(*)(void*);

    /// @brief Type-erased function pointer for move-constructing from one buffer to another.
    using MoveFn = void(*)(void* dst, void* src);

    /**
     * @brief Typed invoker template that calls the callable stored in the buffer.
     * @tparam F The callable type.
     * @param ptr Raw pointer to the stored callable.
     */
    template<typename F>
    static void typed_invoke(void* ptr) {
        (*static_cast<F*>(ptr))();
    }

    /**
     * @brief Typed destroyer template that calls the destructor of the callable.
     * @tparam F The callable type.
     * @param ptr Raw pointer to the stored callable.
     */
    template<typename F>
    static void typed_destroy(void* ptr) {
        static_cast<F*>(ptr)->~F();
    }

    /**
     * @brief Typed mover template that move-constructs a callable from src to dst.
     * @tparam F The callable type.
     * @param dst Destination buffer.
     * @param src Source buffer.
     */
    template<typename F>
    static void typed_move(void* dst, void* src) {
        new (dst) F(std::move(*static_cast<F*>(src)));
        static_cast<F*>(src)->~F();
    }

public:
    /**
     * @brief Default constructor. Creates an empty (no-op) TaskWrapper.
     */
    TaskWrapper() : invoke_(nullptr), destroy_(nullptr), move_(nullptr) {}

    /**
     * @brief Constructs a TaskWrapper from any callable.
     *
     * If the callable fits in the inline buffer, it is stored directly via
     * placement new. If it exceeds the buffer, a std::function<void()> is
     * constructed in the buffer instead (which handles its own storage).
     *
     * @tparam F The callable type (must be invocable with no arguments).
     * @param f The callable to wrap.
     */
    template<typename F,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, TaskWrapper>>>
    TaskWrapper(F&& f) {
        using DecayedF = std::decay_t<F>;

        if constexpr (sizeof(DecayedF) <= k_task_buffer_size &&
                      alignof(DecayedF) <= alignof(std::max_align_t) &&
                      std::is_nothrow_move_constructible_v<DecayedF>) {
            // Fast path: fits in inline buffer.
            new (buffer_) DecayedF(std::forward<F>(f));
            invoke_ = &typed_invoke<DecayedF>;
            destroy_ = &typed_destroy<DecayedF>;
            move_ = &typed_move<DecayedF>;
        } else {
            // Slow path: wrap in std::function (rare for game jobs).
            static_assert(sizeof(std::function<void()>) <= k_task_buffer_size,
                          "k_task_buffer_size must be large enough for std::function<void()>");
            new (buffer_) std::function<void()>(std::forward<F>(f));
            invoke_ = &typed_invoke<std::function<void()>>;
            destroy_ = &typed_destroy<std::function<void()>>;
            move_ = &typed_move<std::function<void()>>;
        }
    }

    /// @brief Destructor. Destroys the stored callable if present.
    ~TaskWrapper() {
        if (destroy_) {
            destroy_(buffer_);
        }
    }

    /// @brief Move constructor. Transfers ownership of the callable.
    TaskWrapper(TaskWrapper&& other) noexcept
        : invoke_(other.invoke_),
          destroy_(other.destroy_),
          move_(other.move_)
    {
        if (move_) {
            move_(buffer_, other.buffer_);
        }
        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
        other.move_ = nullptr;
    }

    /// @brief Move assignment. Destroys current callable and takes ownership.
    TaskWrapper& operator=(TaskWrapper&& other) noexcept {
        if (this != &other) {
            if (destroy_) {
                destroy_(buffer_);
            }
            invoke_ = other.invoke_;
            destroy_ = other.destroy_;
            move_ = other.move_;
            if (move_) {
                move_(buffer_, other.buffer_);
            }
            other.invoke_ = nullptr;
            other.destroy_ = nullptr;
            other.move_ = nullptr;
        }
        return *this;
    }

    /// @brief Non-copyable.
    TaskWrapper(const TaskWrapper&) = delete;

    /// @brief Non-copyable.
    TaskWrapper& operator=(const TaskWrapper&) = delete;

    /**
     * @brief Invokes the stored callable.
     * @note No-op if the wrapper is empty.
     */
    void operator()() {
        if (invoke_) {
            invoke_(buffer_);
        }
    }

    /**
     * @brief Checks if the wrapper contains a valid callable.
     * @return True if a callable is stored.
     */
    explicit operator bool() const {
        return invoke_ != nullptr;
    }

private:
    alignas(std::max_align_t) char buffer_[k_task_buffer_size]; /**< Inline callable storage. */
    InvokeFn invoke_;    /**< Type-erased invoke function pointer. */
    DestroyFn destroy_;  /**< Type-erased destroy function pointer. */
    MoveFn move_;        /**< Type-erased move function pointer. */
};

/**
 * @struct Job
 * @brief Represents a single unit of work to be executed by a worker thread.
 *
 * Optimized for cache-friendly layout and zero-allocation submission:
 * - Uses TaskWrapper instead of std::function to avoid heap allocations.
 * - Uses a fixed-capacity inline array for dependencies (max k_max_inline_dependencies).
 * - All fields are laid out for minimal padding.
 */
struct Job {
    TaskWrapper task;    /**< The work callback function to perform. */
    JobHandle handle;    /**< The handle this job contributes to (decremented upon completion). */
    JobType type;        /**< The category/type of this job. */
    uint8_t dep_count;   /**< Number of active dependencies in the deps array. */
    JobHandle deps[k_max_inline_dependencies]; /**< Inline dependency handle storage. */

    /**
     * @brief Default constructor initializing type to 0 and dep_count to 0.
     */
    Job() : type(0), dep_count(0) {}

    /**
     * @brief Constructs a Job with a callback, type, handle, and inline dependencies.
     * @tparam F Callable type.
     * @param fn Task callback function.
     * @param t Job category type.
     * @param h Output completion handle.
     * @param dependencies Pointer to an array of dependency handles.
     * @param num_deps Number of dependencies (clamped to k_max_inline_dependencies).
     */
    template<typename F>
    Job(F&& fn, JobType t, JobHandle h, const JobHandle* dependencies = nullptr, uint8_t num_deps = 0)
        : task(std::forward<F>(fn)),
          handle(h),
          type(t),
          dep_count(num_deps > k_max_inline_dependencies ? k_max_inline_dependencies : num_deps)
    {
        for (uint8_t i = 0; i < dep_count; ++i) {
            deps[i] = dependencies[i];
        }
    }

    /// @brief Non-copyable (TaskWrapper is non-copyable).
    Job(const Job&) = delete;

    /// @brief Non-copyable.
    Job& operator=(const Job&) = delete;

    /// @brief Move constructor.
    Job(Job&& other) noexcept = default;

    /// @brief Move assignment.
    Job& operator=(Job&& other) noexcept = default;

    /**
     * @brief Checks whether all dependency handles have completed.
     * @return True if all deps are complete (or if there are no deps).
     */
    bool are_dependencies_met() const {
        for (uint8_t i = 0; i < dep_count; ++i) {
            if (!deps[i].is_complete()) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Checks whether this job has any dependencies.
     * @return True if dep_count > 0.
     */
    bool has_dependencies() const {
        return dep_count > 0;
    }
};

} // namespace job
} // namespace trav

#endif // JOB_H