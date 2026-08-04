/**
 * @file scheduler.h
 * @brief Automatic hazard-aware job scheduler and dependency resolver.
 *
 * Manages batches of jobs with automatic dependency injection based on
 * component resource access patterns (RAW, WAR, WAW hazards). Includes
 * per-frame lifecycle management to prevent memory leaks from accumulated
 * tracking state.
 */

#ifndef JOBS_SCHEDULER_H
#define JOBS_SCHEDULER_H

#include <vector>
#include <functional>
#include <memory>
#include <numeric>
#include <mutex>
#include <iostream>
#include <algorithm>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

#include <coopa/job/job.h>
#include <coopa/job/handle.h>
#include <coopa/job/engine.h>
#include <coopa/job/platform.h>

namespace coopa {
namespace job {

/**
 * @struct JobNodeDefinition
 * @brief Defines a job's task callback and its component read/write access patterns.
 *
 * Used by JobScheduler to dynamically trace dependency hazards across jobs.
 */
struct JobNodeDefinition {
    std::function<void()> task; /**< Callback representing the job's work. */
    JobType type;               /**< Job type category. */
    std::vector<std::type_index> read_component_types;  /**< Component type IDs this job reads. */
    std::vector<std::type_index> write_component_types; /**< Component type IDs this job writes. */
    bool is_main_thread_job; /**< True if this job must execute on the main/render thread. */

    /**
     * @brief Default constructor.
     */
    JobNodeDefinition() : type(0), is_main_thread_job(false) {}

    /**
     * @brief Constructs an explicit JobNodeDefinition.
     * @param fn Task callback.
     * @param t Job type tag.
     * @param reads Component types for read-access.
     * @param writes Component types for write-access.
     * @param is_main_thread Set to true if main thread execution is required.
     */
    JobNodeDefinition(
        std::function<void()> fn,
        JobType t,
        std::vector<std::type_index> reads = {},
        std::vector<std::type_index> writes = {},
        bool is_main_thread = false
    ) :
        task(std::move(fn)),
        type(t),
        read_component_types(std::move(reads)),
        write_component_types(std::move(writes)),
        is_main_thread_job(is_main_thread)
    {}
};

/**
 * @struct JobNode
 * @brief Represents a job definition combined with its submitted JobHandle.
 */
struct JobNode {
    JobNodeDefinition definition; /**< The job definition. */
    JobHandle handle; /**< The associated job handle tracking completion status. */
};

/**
 * @class JobScheduler
 * @brief Manages a batch of jobs and their dependencies based on component resource access.
 *
 * The scheduler automatically forms dependencies by tracking component resource contention:
 * - Read-After-Write (RAW): a reader must wait for the prior writer.
 * - Write-After-Read (WAR): a writer must wait for all prior readers.
 * - Write-After-Write (WAW): a writer must wait for the prior writer.
 *
 * Includes explicit begin_frame()/end_frame() lifecycle methods to prevent
 * unbounded accumulation of tracking state (memory leak fix).
 */
class JobScheduler {
public:
    /**
     * @brief Constructs a JobScheduler associated with a JobEngine.
     * @param engine The JobEngine reference where jobs will be submitted.
     */
    JobScheduler(JobEngine& engine) : engine_(engine) {}

    // --- Frame Lifecycle ---

    /**
     * @brief Signals the start of a new frame.
     *
     * Clears all persistent hazard-tracking state (last-writer and active-reader
     * maps) to prevent unbounded memory growth. Also delegates to the engine's
     * begin_frame() to reset the counter pool.
     *
     * Must be called at the start of each frame/update cycle before any add_job() calls.
     */
    void begin_frame() {
        std::lock_guard<std::mutex> lock(m_mutex_);

        persistent_last_writer_handles_.clear();
        persistent_active_reader_handles_.clear();

        engine_.begin_frame();
    }

    /**
     * @brief Signals the end of a frame.
     *
     * Clears any residual main-thread job queue and delegates to the engine's
     * end_frame(). Must be called after all frame work is complete.
     */
    void end_frame() {
        std::lock_guard<std::mutex> lock(m_mutex_);

        main_thread_jobs_queue_.clear();
        pending_job_definitions_.clear();

        engine_.end_frame();
    }

    // --- Job Registration ---

    /**
     * @brief Enqueues a job definition.
     *
     * Dependencies are automatically calculated upon submission based on
     * read/write component access patterns.
     *
     * @param task The work function callback.
     * @param type The category tag of the job.
     * @param read_types Component Type IDs this job reads.
     * @param write_types Component Type IDs this job writes.
     * @param main_thread True if execution must occur on the main thread.
     */
    void add_job(
        std::function<void()> task,
        JobType type,
        const std::vector<std::type_index>& read_types = {},
        const std::vector<std::type_index>& write_types = {},
        bool main_thread = false
    ) {
        std::lock_guard<std::mutex> lock(m_mutex_);

        pending_job_definitions_.emplace_back(
            std::move(task),
            type,
            read_types,
            write_types,
            main_thread
        );
    }

    // --- Submission ---

    /**
     * @brief Resolves dependency hazards and submits all enqueued jobs to the JobEngine.
     *
     * Builds a dependency graph by analyzing read/write access patterns across
     * all pending job definitions, then submits each job with its computed
     * dependencies.
     *
     * @return A vector of JobHandles representing the submitted jobs.
     */
    std::vector<JobHandle> submit_all() {
        std::lock_guard<std::mutex> lock(m_mutex_);

        std::vector<JobHandle> new_job_handles;
        new_job_handles.reserve(pending_job_definitions_.size());

        // Move persistent state into locals for this batch.
        std::unordered_map<std::type_index, JobHandle> last_writer_handles = std::move(persistent_last_writer_handles_);
        std::unordered_map<std::type_index, std::vector<JobHandle>> active_reader_handles = std::move(persistent_active_reader_handles_);

        // Pre-compute write type set for efficient reader-is-also-writer checks.
        std::unordered_set<std::type_index> write_type_set;

        for (JobNodeDefinition& def : pending_job_definitions_) {
            std::vector<JobHandle> dependencies;

            // RAW hazard: wait for the last writer of any component we read.
            for (const std::type_index& read_type : def.read_component_types) {
                auto it = last_writer_handles.find(read_type);
                if (it != last_writer_handles.end()) {
                    dependencies.push_back(it->second);
                }
            }

            // WAW and WAR hazards: wait for readers and writers of components we write.
            for (const std::type_index& write_type : def.write_component_types) {
                // WAR: wait for all active readers.
                auto it_readers = active_reader_handles.find(write_type);
                if (it_readers != active_reader_handles.end()) {
                    dependencies.insert(dependencies.end(),
                                        it_readers->second.begin(),
                                        it_readers->second.end());
                    active_reader_handles.erase(it_readers);
                }

                // WAW: wait for the last writer.
                auto it_writer = last_writer_handles.find(write_type);
                if (it_writer != last_writer_handles.end()) {
                    dependencies.push_back(it_writer->second);
                }
            }

            // Deduplicate dependencies using handle index for O(N log N).
            std::sort(dependencies.begin(), dependencies.end(),
                      [](const JobHandle& a, const JobHandle& b) {
                          return a.index() < b.index();
                      });
            dependencies.erase(
                std::unique(dependencies.begin(), dependencies.end(),
                            [](const JobHandle& a, const JobHandle& b) {
                                return a.index() == b.index();
                            }),
                dependencies.end());

            // Clamp dependencies to inline capacity.
            uint8_t dep_count = static_cast<uint8_t>(
                std::min(dependencies.size(),
                         static_cast<std::size_t>(k_max_inline_dependencies)));

            // Create a handle for this job.
            JobHandle self_handle = engine_.create_handle();

            if (def.is_main_thread_job) {
                // Main-thread job: wrap with dependency waiting.
                auto* counter = self_handle.get_counter();
                if (counter) {
                    counter->fetch_add(1, std::memory_order_release);
                }

                // Capture dependencies and task for main-thread execution.
                std::vector<JobHandle> captured_deps(dependencies.begin(),
                                                      dependencies.begin() + dep_count);
                auto captured_task = std::move(def.task);
                JobEngine* engine_ptr = &engine_;

                std::function<void()> wrapped_task = [
                    task = std::move(captured_task),
                    dep_handles = std::move(captured_deps),
                    engine_ref = engine_ptr
                ]() mutable {
                    for (const auto& handle : dep_handles) {
                        engine_ref->wait_for(handle);
                    }
                    task();
                };

                JobNode node;
                node.definition = def;
                node.definition.task = std::move(wrapped_task);
                node.handle = self_handle;
                main_thread_jobs_queue_.push_back(std::move(node));

            } else {
                // Worker-thread job: submit to engine with dependencies.
                engine_.submit(
                    std::move(def.task),
                    def.type,
                    self_handle,
                    dep_count > 0 ? dependencies.data() : nullptr,
                    dep_count
                );
            }

            new_job_handles.push_back(self_handle);

            // Update tracking maps.
            for (const std::type_index& write_type : def.write_component_types) {
                last_writer_handles[write_type] = self_handle;
            }

            // Build write set for this job.
            write_type_set.clear();
            for (const std::type_index& wt : def.write_component_types) {
                write_type_set.insert(wt);
            }

            for (const std::type_index& read_type : def.read_component_types) {
                // Only track as reader if not also a writer of the same component.
                if (write_type_set.find(read_type) == write_type_set.end()) {
                    active_reader_handles[read_type].push_back(self_handle);
                }
            }
        }

        // Persist state for the next submit_all() call within this frame.
        persistent_last_writer_handles_ = std::move(last_writer_handles);
        persistent_active_reader_handles_ = std::move(active_reader_handles);
        pending_job_definitions_.clear();

        return new_job_handles;
    }

    // --- Main Thread Execution ---

    /**
     * @brief Executes all enqueued main-thread jobs sequentially.
     *
     * This function must be called on the main/render thread once per update
     * loop. Jobs are executed in submission order.
     */
    void execute_main_thread_jobs() {
        std::lock_guard<std::mutex> lock(m_mutex_);

        for (JobNode& node : main_thread_jobs_queue_) {
            node.definition.task();
            auto* counter = node.handle.get_counter();
            if (counter) {
                counter->fetch_sub(1, std::memory_order_release);
            }
        }

        main_thread_jobs_queue_.clear();
    }

    // --- Waiting ---

    /**
     * @brief Blocks the current thread until all handles in the batch are completed.
     * @param batch_handles List of job handles to wait for.
     */
    void wait_for_all(const std::vector<JobHandle>& batch_handles) {
        for (const auto& h : batch_handles) {
            engine_.wait_for(h);
        }
    }

private:
    JobEngine& engine_; /**< Reference to the core job engine. */
    std::vector<JobNodeDefinition> pending_job_definitions_; /**< Pending job definitions. */
    std::vector<JobNode> main_thread_jobs_queue_; /**< Queue of main-thread-only jobs. */
    mutable std::mutex m_mutex_; /**< Mutex for thread-safety. */

    /// @brief Tracks last writer handle per component type (cleared per-frame via begin_frame).
    std::unordered_map<std::type_index, JobHandle> persistent_last_writer_handles_;

    /// @brief Tracks active reader handles per component type (cleared per-frame via begin_frame).
    std::unordered_map<std::type_index, std::vector<JobHandle>> persistent_active_reader_handles_;
};

} // namespace job
} // namespace coopa

#endif // JOBS_SCHEDULER_H