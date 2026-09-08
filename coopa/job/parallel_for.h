/**
 * @file parallel_for.h
 * @brief Out-of-line definitions of JobEngine::parallel_for()/parallel_for_blocking(),
 *        declared in engine.h.
 *
 * Replaces the hand-chunking pattern every parallel call site used to write
 * for itself (see e.g. the pre-existing AnimationSystem::evaluate_parallel_(),
 * which built a `std::vector<std::function<void()>>` and called
 * submit_jobs()). The body is captured exactly once into a shared holder
 * rather than copied into every chunk, and each chunk's own closure (a
 * shared_ptr plus two indices) is small enough to stay inside TaskWrapper's
 * inline buffer -- no heap allocation per chunk.
 */

#ifndef COOPA_JOB_PARALLEL_FOR_H
#define COOPA_JOB_PARALLEL_FOR_H

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include <coopa/job/engine.h>

namespace coopa {
namespace job {

template<typename F>
JobHandle JobEngine::parallel_for(size_t count, size_t grain, F&& body,
                                    JobType type, Priority priority) {
    JobHandle handle = create_handle();
    if (count == 0 || !handle.is_valid()) return handle;

    if (grain == 0) {
        size_t workers = worker_count() > 0 ? worker_count() : 1;
        grain = count / (workers * 4);
        if (grain == 0) grain = 1;
    }

    // NOTE: do NOT also add_jobs_() here for the chunk count -- submit()
    // below already increments the handle's job counter by 1 per call. Doing
    // both double-counts: the counter would settle at num_chunks once every
    // chunk actually completes, instead of 0, and wait_for() would spin
    // forever waiting for a zero that can never arrive.
    using Body = std::decay_t<F>;
    auto shared_body = std::make_shared<Body>(std::forward<F>(body));

    for (size_t start = 0; start < count; start += grain) {
        size_t end = std::min(start + grain, count);
        submit(
            [shared_body, start, end](const JobContext& ctx) {
                if constexpr (std::is_invocable_v<Body&, size_t, size_t, const JobContext&>) {
                    (*shared_body)(start, end, ctx);
                } else {
                    (*shared_body)(start, end);
                }
            },
            type, handle, nullptr, 0u, priority);
    }
    return handle;
}

template<typename F>
void JobEngine::parallel_for_blocking(size_t count, size_t grain, F&& body,
                                        JobType type, Priority priority) {
    JobHandle handle = parallel_for(count, grain, std::forward<F>(body), type, priority);
    wait_for(handle);
    handle.close();
}

} // namespace job
} // namespace coopa

#endif // COOPA_JOB_PARALLEL_FOR_H
