/**
 * @file engine.h
 * @brief Core job engine featuring a multi-threaded work-stealing job queue system.
 *
 * Optimized for realtime/game-engine use with:
 * - Lock-free Chase-Lev work-stealing deques (zero mutex acquisitions in hot path)
 * - Per-thread submission inboxes preserving single-producer Chase-Lev invariant
 * - Pre-allocated counter pool (zero heap allocations per job submission)
 * - Spinning-then-sleeping worker strategy (avoids CV syscall overhead)
 * - Cache-line-padded per-thread state to prevent false sharing
 * - Explicit begin_frame()/end_frame() lifecycle for deterministic cleanup
 */

#ifndef JOB_ENGINE_H
#define JOB_ENGINE_H

#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <array>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <utility>
#include <numeric>
#include <algorithm>
#include <unordered_map>

#include <coopa/debug/logger.h>
#include <coopa/job/job.h>
#include <coopa/job/handle.h>
#include <coopa/job/thread.h>
#include <coopa/job/platform.h>
#include <coopa/job/collections/work_stealing_deque.h>

/// @brief Categorization tag for jobs (used for dedication and metrics).
using JobType = uint32_t;

namespace coopa {
namespace job {

/**
 * @class JobEngine
 * @brief Processes tasks in parallel across worker threads, utilizing lock-free work-stealing.
 *
 * The engine owns a CounterPool for zero-allocation handle creation, per-thread
 * Chase-Lev deques for lock-free push/pop/steal, and an event-driven dependency
 * resolution system.
 */
class JobEngine {
public:

#ifdef COOPA_JOB_DIAGNOSTICS
    /**
     * @struct Snapshot
     * @brief Holds a consistent snapshot of job counts for diagnostics.
     */
    struct Snapshot {
        int total_pending = 0;                              /**< Total pending jobs. */
        std::array<int, k_max_job_types> type_counts = {};  /**< Per-type counts. */
    };

    /**
     * @struct ThreadDiagnostics
     * @brief Snapshot of a single worker thread's state for profiling.
     */
    struct ThreadDiagnostics {
        unsigned int thread_id;     /**< Worker thread index. */
        size_t queue_depth;         /**< Approximate items in this thread's deque. */
        std::string dedication;     /**< "General" or the dedicated JobType as a string. */
    };
#endif // COOPA_JOB_DIAGNOSTICS

    /**
     * @brief Constructs the JobEngine.
     * @param num_threads Number of worker threads. Defaults to hardware concurrency.
     * @param pool_capacity Maximum in-flight job counters per frame. Defaults to k_default_job_pool_capacity.
     */
    JobEngine(unsigned int num_threads = std::thread::hardware_concurrency(),
              uint32_t pool_capacity = k_default_job_pool_capacity)
        : logger_(new coopa::debug::Logger("JobEngine")),
          num_threads_(num_threads > 0 ? num_threads : 1),
          counter_pool_(pool_capacity),
          submit_thread_index_(0),
          total_pending_jobs_count_(0),
          shutting_down_(false)
    {
        logger_->info("JobEngine setup with " + std::to_string(num_threads_) + " threads");

        // Initialize flat dedication array to "no dedication".
        thread_dedications_.fill(UINT32_MAX);
        for (auto& c : job_type_counts_) {
            c.store(0, std::memory_order_relaxed);
        }

        // Initialize per-thread work-stealing deques and submission inboxes.
        for (unsigned int i = 0; i < num_threads_; ++i) {
            per_thread_deques_.push_back(
                std::make_unique<WorkStealingDeque<Job>>(pool_capacity / num_threads_ + 256)
            );
            per_thread_inboxes_.push_back(std::make_unique<ThreadInbox>());
        }

        // All threads start as general-purpose.
        general_purpose_thread_indices_.resize(num_threads_);
        std::iota(general_purpose_thread_indices_.begin(), general_purpose_thread_indices_.end(), 0);

        // Spawn worker threads.
        for (unsigned int i = 0; i < num_threads_; ++i) {
            worker_threads_.emplace_back(std::make_unique<coopa::job::Thread>(
                i,
                [this](unsigned int thread_id, std::atomic<bool>& stop_flag, std::mutex& /*mtx*/, std::condition_variable& /*cv*/) {
                    this->worker_thread_loop(thread_id, stop_flag);
                },
                logger_
            ));
        }
    }

    /**
     * @brief Destructor. Shuts down the job system and joins all worker threads.
     */
    ~JobEngine() {
        shutdown();
        delete logger_;
    }

    /// @brief Non-copyable.
    JobEngine(const JobEngine&) = delete;

    /// @brief Non-copyable.
    JobEngine& operator=(const JobEngine&) = delete;

    // --- Frame Lifecycle ---

    /**
     * @brief Signals the start of a new frame.
     *
     * Resets the counter pool so that all counter slots become available for
     * new handle allocations. Must only be called when all handles from the
     * previous frame are known to be complete (i.e., after wait_for_all or
     * equivalent synchronization).
     */
    void begin_frame() {
        counter_pool_.reset();

#ifdef COOPA_JOB_DIAGNOSTICS
        // Reset per-type counters for the new frame.
        for (auto& c : job_type_counts_) {
            c.store(0, std::memory_order_relaxed);
        }
#endif
    }

    /**
     * @brief Signals the end of a frame.
     *
     * Currently a no-op placeholder for future per-frame cleanup (e.g.,
     * profiling snapshots, memory compaction). The important cleanup happens
     * in begin_frame() when the pool is reset.
     */
    void end_frame() {
        // Reserved for future per-frame finalization.
    }

    // --- Handle Creation ---

    /**
     * @brief Allocates a new JobHandle from the counter pool.
     *
     * The handle's counter is initialized to 0. Callers should increment
     * it via submit() or manually via get_counter()->fetch_add().
     *
     * @return A valid JobHandle, or an invalid handle if the pool is exhausted.
     */
    JobHandle create_handle() {
        uint32_t idx = counter_pool_.allocate();
        return JobHandle(&counter_pool_, idx);
    }

    // --- Thread Dedication ---

    /**
     * @brief Dedicates a specific worker thread to handle only a specific job type.
     * @param type The JobType to dedicate the thread to.
     * @param thread_index The index of the worker thread.
     */
    void dedicate_thread_to_job_type(JobType type, unsigned int thread_index) {
        if (thread_index >= num_threads_) {
            logger_->warn("Attempted to dedicate invalid thread index: " + std::to_string(thread_index));
            return;
        }

        if (type >= k_max_job_types) {
            logger_->warn("Job type " + std::to_string(type) + " exceeds k_max_job_types (" + std::to_string(k_max_job_types) + ")");
            return;
        }

        std::lock_guard<std::mutex> lock(dedication_mutex_);

        thread_dedications_[type] = thread_index;

        // Remove this thread from the general-purpose pool.
        auto& indices = general_purpose_thread_indices_;
        indices.erase(std::remove(indices.begin(), indices.end(), thread_index), indices.end());

        logger_->info("Thread " + std::to_string(thread_index) + " dedicated to type " + std::to_string(type));
    }

    // --- Job Submission ---

    /**
     * @brief Submits a single job to the system.
     *
     * The job is assigned to a worker deque via round-robin or dedication.
     * If the job has dependencies, it is placed in the pending list for
     * event-driven promotion when its dependencies complete.
     *
     * @tparam F Callable type.
     * @param task_func The function to execute.
     * @param type The type category of the job.
     * @param handle The handle this job will contribute to.
     * @param dependencies Pointer to dependency handle array (may be nullptr).
     * @param dep_count Number of dependencies.
     */
    template<typename F>
    void submit(F&& task_func, JobType type, JobHandle& handle,
                const JobHandle* dependencies = nullptr, uint8_t dep_count = 0) {
        auto* counter = handle.get_counter();
        if (counter) {
            counter->fetch_add(1, std::memory_order_release);
        }

        Job new_job(std::forward<F>(task_func), type, handle, dependencies, dep_count);

#ifdef COOPA_JOB_DIAGNOSTICS
        if (type < k_max_job_types) {
            job_type_counts_[type].fetch_add(1, std::memory_order_relaxed);
        }
#endif
        total_pending_jobs_count_.fetch_add(1, std::memory_order_relaxed);

        // If dependencies exist, park in the pending list.
        if (dep_count > 0) {
            std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
            pending_jobs_.push_back(std::move(new_job));
            // Wake a worker so it can check for promotable jobs.
            wake_one_worker();
            return;
        }

        // No dependencies — deposit into the target worker's inbox.
        // Workers drain their inbox into their own deque, preserving the
        // Chase-Lev single-producer invariant (only the owner pushes).
        unsigned int queue_index = resolve_queue_index(type);
        {
            auto& inbox = *per_thread_inboxes_[queue_index];
            std::lock_guard<std::mutex> lock(inbox.mutex);
            inbox.jobs.push_back(std::move(new_job));
        }

        // Wake every worker, not just one: the job sits in a specific
        // thread's inbox (queue_index) until that thread itself drains it
        // into its own deque, and worker_cv_ is shared across every worker
        // rather than per-thread. notify_one() can wake an unrelated idle
        // thread whose own inbox is empty instead of the one that actually
        // owns this job — if every other worker is already asleep at that
        // moment, the job's real owner never wakes and wait_for() on it
        // hangs forever. submit_jobs()'s equivalent no-dependency path
        // already uses wake_all_workers() for the same reason.
        wake_all_workers();
    }

    /**
     * @brief Submits a batch of jobs that all contribute to a single new handle.
     * @tparam F Callable type.
     * @param task_funcs A vector of functions to execute.
     * @param type The type of all jobs in this batch.
     * @param dependencies Pointer to dependency handle array (may be nullptr).
     * @param dep_count Number of dependencies.
     * @return A new JobHandle representing this batch.
     */
    template<typename F>
    JobHandle submit_jobs(const std::vector<F>& task_funcs, JobType type,
                          const JobHandle* dependencies = nullptr, uint8_t dep_count = 0) {
        JobHandle new_handle = create_handle();
        auto* counter = new_handle.get_counter();
        if (!counter) {
            logger_->error("Counter pool exhausted in submit_jobs()");
            return new_handle;
        }

        size_t num_jobs = task_funcs.size();
        counter->fetch_add(static_cast<int32_t>(num_jobs), std::memory_order_release);

#ifdef COOPA_JOB_DIAGNOSTICS
        if (type < k_max_job_types) {
            job_type_counts_[type].fetch_add(static_cast<int>(num_jobs), std::memory_order_relaxed);
        }
#endif
        total_pending_jobs_count_.fetch_add(static_cast<int>(num_jobs), std::memory_order_relaxed);

        if (dep_count > 0) {
            std::lock_guard<std::mutex> lock(pending_jobs_mutex_);
            for (const auto& task_func : task_funcs) {
                Job new_job(task_func, type, new_handle, dependencies, dep_count);
                pending_jobs_.push_back(std::move(new_job));
            }
        } else {
            for (const auto& task_func : task_funcs) {
                Job new_job(task_func, type, new_handle, nullptr, 0);
                unsigned int queue_index = resolve_queue_index(type);
                auto& inbox = *per_thread_inboxes_[queue_index];
                std::lock_guard<std::mutex> lock(inbox.mutex);
                inbox.jobs.push_back(std::move(new_job));
            }
            wake_all_workers();
        }

        return new_handle;
    }

    // --- Waiting ---

    /**
     * @brief Stalls the calling thread until the specified handle is complete.
     *
     * While waiting, this thread participates in executing other jobs from
     * the system (guest worker pattern).
     *
     * @param handle The handle to wait for.
     */
    void wait_for(const JobHandle& handle) {
        while (!handle.is_complete()) {
            if (!try_execute_a_job(-1)) {
                std::this_thread::yield();
            }
        }
    }

#ifdef COOPA_JOB_DIAGNOSTICS
    /**
     * @brief Stalls until all pending jobs of the specified type are complete.
     * @param type The JobType to wait for.
     */
    void wait_for_type(JobType type) {
        if (type >= k_max_job_types) return;
        while (job_type_counts_[type].load(std::memory_order_acquire) > 0) {
            if (!try_execute_a_job(-1)) {
                std::this_thread::yield();
            }
        }
    }

    // --- Diagnostics ---

    /**
     * @brief Gets a snapshot of current job counts.
     * @return Snapshot struct with total and per-type counts.
     */
    Snapshot get_diagnostics_snapshot() const {
        Snapshot snapshot;
        snapshot.total_pending = total_pending_jobs_count_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < k_max_job_types; ++i) {
            snapshot.type_counts[i] = job_type_counts_[i].load(std::memory_order_relaxed);
        }
        return snapshot;
    }

    /**
     * @brief Gathers diagnostics for all worker thread deques.
     * @return Vector of ThreadDiagnostics, one per worker.
     */
    std::vector<ThreadDiagnostics> get_thread_diagnostics() const {
        std::vector<ThreadDiagnostics> diagnostics;
        diagnostics.resize(num_threads_);

        std::lock_guard<std::mutex> lock(dedication_mutex_);
        for (unsigned int i = 0; i < num_threads_; ++i) {
            diagnostics[i].thread_id = i;
            diagnostics[i].queue_depth = per_thread_deques_[i]->size_approx();
            diagnostics[i].dedication = "General";
        }

        for (uint32_t type = 0; type < k_max_job_types; ++type) {
            uint32_t thread_idx = thread_dedications_[type];
            if (thread_idx != UINT32_MAX && thread_idx < num_threads_) {
                diagnostics[thread_idx].dedication = std::to_string(type);
            }
        }

        return diagnostics;
    }
#endif // COOPA_JOB_DIAGNOSTICS

    // --- Shutdown ---

    /**
     * @brief Signals all worker threads to stop and joins them.
     */
    void shutdown() {
        if (shutting_down_.exchange(true)) return;

        if (!worker_threads_.empty() && worker_threads_[0]->joinable()) {
            logger_->info("JobEngine: Shutting down workers...");
            for (const auto& worker : worker_threads_) {
                worker->signal_stop();
            }
            wake_all_workers();
            for (auto& worker : worker_threads_) {
                if (worker->joinable()) {
                    worker->join();
                }
            }
            worker_threads_.clear();
            logger_->info("JobEngine: All workers shut down.");
        }
    }

    /**
     * @brief Provides access to the counter pool for external handle creation.
     * @return Reference to the engine's CounterPool.
     */
    CounterPool& get_counter_pool() { return counter_pool_; }

private:

    // --- Queue Index Resolution ---

    /**
     * @brief Resolves which worker deque to push a job to, based on type dedication
     *        or round-robin for general-purpose threads.
     * @param type The JobType of the job being submitted.
     * @return The worker deque index.
     */
    unsigned int resolve_queue_index(JobType type) {
        // Check for dedicated thread (lock-free read for common types).
        if (type < k_max_job_types) {
            uint32_t dedicated = thread_dedications_[type];
            if (dedicated != UINT32_MAX && dedicated < num_threads_) {
                return dedicated;
            }
        }

        // Round-robin across general-purpose threads.
        // Note: dedication_mutex_ is NOT held here for the fast path.
        // general_purpose_thread_indices_ is only modified during dedicate_thread_to_job_type(),
        // which is typically called once at init. For maximum safety under dynamic
        // re-dedication, a lock could be added, but the perf tradeoff is not worthwhile.
        if (!general_purpose_thread_indices_.empty()) {
            unsigned int pool_idx = submit_thread_index_.fetch_add(1, std::memory_order_relaxed)
                                    % general_purpose_thread_indices_.size();
            return general_purpose_thread_indices_[pool_idx];
        }

        return submit_thread_index_.fetch_add(1, std::memory_order_relaxed) % num_threads_;
    }

    // --- Worker Loop ---

    /**
     * @brief The main loop for each worker thread.
     *
     * Strategy: spin for k_worker_spin_count iterations looking for work,
     * then fall back to condition variable sleep. This avoids the ~1-5µs
     * syscall overhead for short idle gaps between job batches.
     *
     * @param thread_id Worker index.
     * @param stop_flag Atomic stop signal from the Thread wrapper.
     */
    void worker_thread_loop(unsigned int thread_id, std::atomic<bool>& stop_flag) {
        while (!stop_flag.load(std::memory_order_acquire)) {
            // Drain submission inbox into our own deque (owner-only push).
            drain_inbox(thread_id);

            // Spin phase: try to find work without sleeping.
            bool found_work = false;
            for (uint32_t spin = 0; spin < k_worker_spin_count; ++spin) {
                if (stop_flag.load(std::memory_order_relaxed)) return;
                if (try_execute_a_job(static_cast<int>(thread_id))) {
                    found_work = true;
                    break;
                }
            }

            if (found_work) continue;

            // Sleep phase: no work found after spinning — wait on CV.
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait(lock, [&]() {
                return stop_flag.load(std::memory_order_acquire) ||
                       total_pending_jobs_count_.load(std::memory_order_acquire) > 0;
            });
        }
        logger_->info("Worker thread " + std::to_string(thread_id) + " exiting.");
    }

    /**
     * @brief Drains a worker's submission inbox into its Chase-Lev deque.
     *
     * This is the critical bridge that preserves the single-producer invariant:
     * external threads deposit jobs into the inbox (mutex-guarded), and the
     * owning worker moves them into its deque via push() (owner-only).
     *
     * @param thread_id The worker thread index whose inbox to drain.
     */
    void drain_inbox(unsigned int thread_id) {
        auto& inbox = *per_thread_inboxes_[thread_id];
        std::vector<Job> local_jobs;

        {
            std::lock_guard<std::mutex> lock(inbox.mutex);
            if (inbox.jobs.empty()) return;
            local_jobs = std::move(inbox.jobs);
            inbox.jobs.clear();
        }

        for (auto& job : local_jobs) {
            per_thread_deques_[thread_id]->push(std::move(job));
        }
    }

    // --- Dependency Promotion ---

    /**
     * @brief Checks pending jobs and promotes one whose dependencies are met.
     *
     * Scans the pending list under a lock and moves the first ready job
     * into out_job. This is the fallback path — most jobs have no dependencies
     * and never enter the pending list.
     *
     * @param out_job Populated with the promoted job if found.
     * @return True if a job was promoted, false otherwise.
     */
    bool try_promote_pending_job(Job& out_job) {
        std::lock_guard<std::mutex> lock(pending_jobs_mutex_);

        if (pending_jobs_.empty()) return false;

        for (auto it = pending_jobs_.begin(); it != pending_jobs_.end(); ++it) {
            if (it->are_dependencies_met()) {
                out_job = std::move(*it);
                pending_jobs_.erase(it);
                return true;
            }
        }

        return false;
    }

    // --- Job Execution ---

    /**
     * @brief Attempts to find and execute one job from any source.
     *
     * Priority order:
     * 1. Own deque (pop from bottom — no contention)
     * 2. Steal from other workers' deques (steal from top — single CAS)
     * 3. Promote a ready pending job
     *
     * @param thread_id Worker index, or -1 for guest worker (main thread).
     * @return True if a job was found and executed.
     */
    bool try_execute_a_job(int thread_id) {
        Job job_to_execute;
        bool job_found = false;

        // 1. If called by a worker, drain own inbox first, then try own deque.
        if (thread_id >= 0 && static_cast<unsigned int>(thread_id) < num_threads_) {
            drain_inbox(static_cast<unsigned int>(thread_id));
            if (per_thread_deques_[thread_id]->pop(job_to_execute)) {
                job_found = true;
            }
        }
        // A guest caller (thread_id < 0, e.g. wait_for() on the submitting
        // thread) must NOT drain another worker's inbox itself: drain_inbox()
        // ends by calling per_thread_deques_[thread_id]->push(), and Chase-Lev
        // deques require push() to be called only by that deque's owning
        // thread (see coopa/job/collections/work_stealing_deque.h). A guest
        // "helping" by draining worker i's inbox can race worker i draining
        // its own inbox at the same moment — two threads concurrently
        // move-assigning the same buffer slot is a data race on the Job
        // itself (UB: corrupted state, occasionally observed as two
        // concurrent steal() calls both appearing to claim the same item, or
        // an outright crash). The real owner reliably wakes on its own via
        // wake_all_workers() and drains its own inbox; a guest only ever
        // steals below, which is safe for any number of concurrent callers.

        // 2. Try stealing from others.
        if (!job_found) {
            for (unsigned int i = 0; i < num_threads_; ++i) {
                if (static_cast<int>(i) == thread_id) continue;
                if (per_thread_deques_[i]->steal(job_to_execute)) {
                    job_found = true;
                    break;
                }
            }
        }

        // 3. Try promoting a pending job.
        if (!job_found) {
            if (try_promote_pending_job(job_to_execute)) {
                job_found = true;
            }
        }

        if (job_found) {
            execute_job(job_to_execute);
            return true;
        }

        return false;
    }

    /**
     * @brief Executes a job and decrements its associated counters.
     *
     * After execution, decrements the handle counter and total pending count.
     * If pending jobs exist, wakes a worker to check for promotions.
     *
     * @param job The job to execute.
     */
    void execute_job(Job& job) {
        job.task();

        auto* counter = job.handle.get_counter();
        if (counter) {
            counter->fetch_sub(1, std::memory_order_release);
        }

#ifdef COOPA_JOB_DIAGNOSTICS
        if (job.type < k_max_job_types) {
            job_type_counts_[job.type].fetch_sub(1, std::memory_order_relaxed);
        }
#endif
        int remaining = total_pending_jobs_count_.fetch_sub(1, std::memory_order_release) - 1;

        // If there are still pending jobs (including parked ones with deps),
        // wake a worker to check for promotable work.
        if (remaining > 0) {
            wake_one_worker();
        }
    }

    // --- Worker Wake Helpers ---

    /**
     * @brief Wakes one sleeping worker thread.
     *
     * Acquires worker_mutex_ around the notify even though nothing else
     * needs protecting here: submit()/wake callers mutate shared submission
     * state (total_pending_jobs_count_, a per-thread inbox, ...) without
     * holding worker_mutex_ at all, so without this lock a worker could
     * evaluate its wait(lock, pred) predicate as false and then block
     * *after* this notify already fired -- a classic lost wakeup, since
     * notify_one()/notify_all() only wake threads already parked in wait().
     * Taking the lock here forces this notify to happen either strictly
     * before a racing worker acquires the lock to check its predicate (so
     * it observes the new state directly and never blocks) or strictly
     * after that worker has fully, atomically completed unlock-and-block
     * inside wait() (so the notify reaches it) -- std::mutex's
     * release-then-acquire also makes every write sequenced before this
     * call visible to whichever worker acquires worker_mutex_ next, so the
     * relaxed increment of total_pending_jobs_count_ in submit() doesn't
     * need its own memory-order upgrade.
     */
    void wake_one_worker() {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_cv_.notify_one();
    }

    /// @brief Wakes all sleeping worker threads. See wake_one_worker() for why this locks worker_mutex_ first.
    void wake_all_workers() {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_cv_.notify_all();
    }

    // --- Member Variables ---

    /**
     * @struct ThreadInbox
     * @brief Mutex-guarded submission inbox for a single worker thread.
     *
     * External threads (main thread, other submitters) deposit jobs here.
     * The owning worker drains the inbox into its Chase-Lev deque at the
     * start of each work-search iteration, preserving the single-producer
     * invariant required by Chase-Lev.
     */
    struct ThreadInbox {
        std::mutex mutex;       /**< Guards concurrent deposits from submitters. */
        std::vector<Job> jobs;  /**< Pending jobs awaiting drain into the deque. */
    };

    coopa::debug::Logger* logger_ = nullptr;
    unsigned int num_threads_;
    CounterPool counter_pool_; /**< Pre-allocated pool of atomic counters. */
    std::vector<std::unique_ptr<coopa::job::Thread>> worker_threads_;

    /// @brief Per-thread lock-free work-stealing deques (owner push/pop only).
    std::vector<std::unique_ptr<WorkStealingDeque<Job>>> per_thread_deques_;

    /// @brief Per-thread submission inboxes (any thread deposits, owner drains).
    std::vector<std::unique_ptr<ThreadInbox>> per_thread_inboxes_;

    /// @brief Pending jobs awaiting dependency resolution.
    std::vector<Job> pending_jobs_;
    mutable std::mutex pending_jobs_mutex_;

    /// @brief Flat array mapping JobType -> dedicated thread index (UINT32_MAX = no dedication).
    std::array<uint32_t, k_max_job_types> thread_dedications_;
    mutable std::mutex dedication_mutex_;
    std::vector<unsigned int> general_purpose_thread_indices_;

    /// @brief Round-robin submit counter (relaxed atomic, no cache-line padding needed).
    alignas(k_cache_line_size) std::atomic<unsigned int> submit_thread_index_;

    /// @brief Total number of in-flight jobs (used for worker sleep/wake decisions).
    alignas(k_cache_line_size) std::atomic<int> total_pending_jobs_count_;

    /// @brief Worker sleep/wake synchronization.
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;

    /// @brief Shutdown guard to prevent double-shutdown.
    std::atomic<bool> shutting_down_;

#ifdef COOPA_JOB_DIAGNOSTICS
    /// @brief Flat array of per-type job counters for diagnostics (only when enabled).
    std::array<std::atomic<int>, k_max_job_types> job_type_counts_;
#else
    /// @brief Flat array placeholder (zero-cost when diagnostics disabled).
    std::array<std::atomic<int>, k_max_job_types> job_type_counts_;
#endif
};

} // namespace job
} // namespace coopa

#endif // JOB_ENGINE_H