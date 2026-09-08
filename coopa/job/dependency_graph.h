/**
 * @file dependency_graph.h
 * @brief Event-driven, unbounded-fan-in job dependency resolution.
 *
 * Replaces the previous design's `pending_jobs_` vector + `pending_jobs_mutex_`
 * + `try_promote_pending_job()`, which scanned the entire pending list under a
 * global lock on every failed work search, and which silently truncated any
 * job's dependency list to k_max_inline_dependencies (4) with no diagnostic.
 *
 * Here, a job with dependencies becomes a PendingNode holding one WaiterNode per
 * dependency. Each WaiterNode is pushed onto the *dependency's own* slot (a
 * lock-free Treiber stack living in CounterPool -- see handle.h's
 * `waiters_slot()`). Whichever thread drives that dependency's job counter to
 * zero pops the whole stack and fires every node on it: no scanning, no global
 * lock, no polling, and no limit on how many dependencies a job may have.
 *
 * The one subtlety (called out inline below) is the register-vs-complete race:
 * a dependency may finish between "we pushed our WaiterNode onto its stack" and
 * "we checked whether it was already done" -- handled by re-checking
 * is_complete() immediately after registering and self-firing anything that
 * raced past us, with each WaiterNode's own `fired` flag guaranteeing exactly
 * one of {the real completer, our own re-check} ever fires it.
 */

#ifndef COOPA_JOB_DEPENDENCY_GRAPH_H
#define COOPA_JOB_DEPENDENCY_GRAPH_H

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <coopa/job/context.h>
#include <coopa/job/handle.h>
#include <coopa/job/job.h>
#include <coopa/job/platform.h>

namespace coopa {
namespace job {

struct PendingNode;

/**
 * @struct WaiterNode
 * @brief One dependency edge: lives on the dependency handle's waiter stack
 *        until fired, at which point it decrements its owning PendingNode's
 *        unmet-dependency count exactly once.
 */
struct WaiterNode {
    PendingNode* node = nullptr;   /**< The job waiting on this dependency. */
    WaiterNode* next = nullptr;    /**< Next entry in the dependency's waiter stack. */
    std::atomic<bool> fired{false}; /**< CAS-guarded single-fire latch. */
};

/**
 * @struct PendingNode
 * @brief A job parked until every one of its dependencies completes.
 *
 * Heap-allocated per dependency-bearing submission (via `new`/`delete`) rather
 * than drawn from a fixed pool: unlike CounterPool, this path is not the
 * per-job hot path (the overwhelming majority of jobs have zero dependencies
 * and never construct a PendingNode at all), so the simplicity of ordinary
 * allocation outweighs pooling it. Owns inline storage for up to
 * k_max_inline_dependencies WaiterNodes (the common case) and spills to
 * separately-allocated nodes beyond that -- WaiterNode addresses must stay
 * stable once pushed onto a dependency's stack, which rules out a
 * reallocating container like std::vector<WaiterNode>.
 */
struct PendingNode {
    Job job;                                  /**< The job to run once unmet reaches 0. */
    std::atomic<int32_t> unmet{0};             /**< Dependencies not yet resolved. */
    WaiterNode inline_waiters[k_max_inline_dependencies];
    std::vector<std::unique_ptr<WaiterNode>> spill_waiters;
};

/**
 * @class DependencyGraph
 * @brief Owns the event-driven dependency resolution machinery for one JobEngine.
 */
class DependencyGraph {
public:
    /// @brief Invoked with a job the instant every one of its dependencies completes.
    using ReadySink = std::function<void(Job&&)>;

    explicit DependencyGraph(ReadySink sink) : on_ready_(std::move(sink)) {}

    /// @brief Non-copyable, non-movable (holds no state that would survive a move safely mid-flight).
    DependencyGraph(const DependencyGraph&) = delete;
    DependencyGraph& operator=(const DependencyGraph&) = delete;

    /**
     * @brief Parks `job` until every handle in `deps[0..dep_count)` completes.
     *
     * @param job The job to run once ready. Moved from.
     * @param deps Dependency handle array. May contain invalid/stale handles
     *             (treated as already complete).
     * @param dep_count Number of dependencies. No upper bound.
     */
    void submit_with_dependencies(Job&& job, const JobHandle* deps, uint32_t dep_count) {
        assert(dep_count > 0 && "submit_with_dependencies() called with no dependencies");

        PendingNode* node = new PendingNode();
        node->job = std::move(job);
        node->unmet.store(static_cast<int32_t>(dep_count), std::memory_order_relaxed);

        if (dep_count > k_max_inline_dependencies) {
            node->spill_waiters.reserve(dep_count - k_max_inline_dependencies);
            for (uint32_t i = k_max_inline_dependencies; i < dep_count; ++i) {
                node->spill_waiters.push_back(std::make_unique<WaiterNode>());
            }
        }

        // Phase 1: register a waiter against every dependency before checking any
        // of them. Registering first (rather than check-then-register per-dep)
        // means the phase-2 re-check below is the ONLY place a completed-during-
        // registration dependency can be missed, and it covers all of them.
        for (uint32_t i = 0; i < dep_count; ++i) {
            WaiterNode* w = waiter_at_(node, i);
            w->node = node;
            w->fired.store(false, std::memory_order_relaxed);
            register_waiter_(deps[i], w);
        }

        // Phase 2: a dependency that completed after we registered (or that was
        // already complete/invalid/stale to begin with) will never be walked by
        // a completer's harvest -- fire it ourselves. fire_waiter_()'s CAS makes
        // this safe to race against a completer that finishes at the same time.
        for (uint32_t i = 0; i < dep_count; ++i) {
            if (deps[i].is_complete()) {
                fire_waiter_(waiter_at_(node, i));
            }
        }
    }

    /**
     * @brief Called by JobEngine right after a handle's job counter hits zero:
     *        pops and fires every waiter registered against it.
     * @param completed The handle whose counter just reached zero.
     */
    void harvest_and_fire(const JobHandle& completed) {
        if (!completed.is_valid()) return;
        std::atomic<void*>& head_slot = completed.pool()->waiters_slot(completed.index());
        void* raw = head_slot.exchange(nullptr, std::memory_order_acq_rel);
        auto* w = static_cast<WaiterNode*>(raw);
        while (w) {
            WaiterNode* next = w->next;
            fire_waiter_(w);
            w = next;
        }
    }

private:
    static WaiterNode* waiter_at_(PendingNode* node, uint32_t i) {
        if (i < k_max_inline_dependencies) return &node->inline_waiters[i];
        return node->spill_waiters[i - k_max_inline_dependencies].get();
    }

    /// @brief Pushes `w` onto `dep`'s slot-local waiter stack. No-op for an
    ///        already-invalid handle (phase 2 will fire it via is_complete()).
    static void register_waiter_(const JobHandle& dep, WaiterNode* w) {
        if (!dep.is_valid()) return;
        std::atomic<void*>& head_slot = dep.pool()->waiters_slot(dep.index());
        void* old_head = head_slot.load(std::memory_order_acquire);
        for (;;) {
            w->next = static_cast<WaiterNode*>(old_head);
            if (head_slot.compare_exchange_weak(old_head, w,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                return;
            }
        }
    }

    /// @brief Fires `w` exactly once, decrementing its PendingNode's unmet count
    ///        and dispatching the job if this was the last dependency.
    void fire_waiter_(WaiterNode* w) {
        bool expected = false;
        if (!w->fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return; // Already fired by the other race path (see class doc).
        }

        PendingNode* node = w->node;
        int32_t remaining = node->unmet.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            Job ready_job = std::move(node->job);
            delete node;
            on_ready_(std::move(ready_job));
        }
    }

    ReadySink on_ready_;
};

} // namespace job
} // namespace coopa

#endif // COOPA_JOB_DEPENDENCY_GRAPH_H
