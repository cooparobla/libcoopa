/**
 * @file animation_system.h
 * @brief The Animation-phase ISceneSystem: batches every Animator's evaluation
 *        across the scene, optionally in parallel via the Scene's JobEngine.
 */

#ifndef COOPA_ANIMATION_ANIMATION_SYSTEM_H
#define COOPA_ANIMATION_ANIMATION_SYSTEM_H

#include <coopa/animation/animator.h>
#include <coopa/job/engine.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_system.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace coopa {
namespace anim {

/**
 * @class AnimationSystem
 * @brief Registered at coopa::scene::UpdatePhase::Animation; the sole driver of every Animator.
 *
 * An Animator has no update()/late_update() of its own — only this system
 * advances it (via the friend-only Animator::advance_()/evaluate_()/apply_()
 * split), and only ever in this fixed order:
 *
 *   1. advance_()  — serial, main thread: clocks, crossfade weights, signals, rebinds.
 *   2. evaluate_() — parallel (via the Scene's JobEngine) once the batch is
 *                    large enough to be worth it, else inline. WORKER-SAFE by
 *                    construction: touches no Component, only const clip data
 *                    and a caller-owned scratch buffer.
 *   3. apply_()    — serial, main thread: writes the accumulated result onto
 *                    every binding's target Component.
 *
 * NEVER calls JobEngine::begin_frame()/end_frame() — Scene alone owns that
 * boundary (see coopa::scene::FrameContext's doc). This system only ever
 * calls create_handle()/submit_jobs()/wait_for() on ctx.jobs, and only
 * within one execute() call — no JobHandle survives past a single frame's
 * begin_frame()/end_frame() pair, since CounterPool is an untagged bump
 * allocator.
 */
class AnimationSystem : public coopa::scene::ISceneSystem {
public:
    const char* system_name() const override { return "Animation"; }

    void on_attach(coopa::scene::Scene&) override { dirty_ = true; }

    void execute(coopa::scene::Scene& scene, const coopa::scene::FrameContext& ctx) override {
        if (dirty_) {
            animators_ = scene.get_components<Animator>();
            dirty_ = false;
        }

        for (Animator* a : animators_) a->advance_(ctx.delta_time);

        offsets_.assign(animators_.size(), 0);
        size_t total_floats = 0;
        size_t total_tracks = 0;
        for (size_t i = 0; i < animators_.size(); ++i) {
            offsets_[i] = total_floats;
            total_floats += animators_[i]->binding_count() * 8;
            total_tracks += animators_[i]->active_track_count();
        }
        scratch_.assign(total_floats, 0.0f);

        if (ctx.jobs && !animators_.empty() && total_tracks >= parallel_threshold_) {
            evaluate_parallel_(*ctx.jobs);
        } else {
            for (size_t i = 0; i < animators_.size(); ++i) {
                animators_[i]->evaluate_(scratch_.data() + offsets_[i]);
            }
        }

        for (size_t i = 0; i < animators_.size(); ++i) {
            animators_[i]->apply_(scratch_.data() + offsets_[i]);
        }
    }

    /**
     * @brief Minimum total active-track count across all Animators before
     *        evaluation is dispatched as jobs instead of run inline.
     *
     * Default 256. A runtime setter rather than a constexpr specifically so
     * a test can force threshold 0 (always parallel) or SIZE_MAX (always
     * serial) and assert the two produce bit-identical results.
     */
    void set_parallel_threshold(size_t tracks) { parallel_threshold_ = tracks; }

    /** @brief How many Animators' worth of evaluate_() calls go into one job. Default 32. */
    void set_chunk_size(size_t animators) { chunk_size_ = animators > 0 ? animators : 1; }

    /** @brief Re-gathers the Animator list. Call after adding/removing Animators or toggling set_active(). */
    void refresh() { dirty_ = true; }

    size_t animator_count() const { return animators_.size(); }

    /**
     * @brief Debug aid: one string per (object, component_type, property) written by more than one Animator.
     *
     * Dedup within a single Animator is automatic (see Animator::evaluate_()'s
     * doc) — this catches the case that ISN'T automatically resolved: two
     * different Animators both targeting the same property, which still
     * clobber each other by execution order.
     */
    std::vector<std::string> find_conflicts() const {
        std::unordered_map<std::string, int> counts;
        for (Animator* a : animators_) {
            for (const auto& b : a->bindings_) {
                std::string key = (b.object ? b.object->name() : std::string("?")) + "|" +
                                   b.prop->component_type + "." + b.prop->name;
                ++counts[key];
            }
        }
        std::vector<std::string> result;
        for (const auto& kv : counts) {
            if (kv.second > 1) result.push_back(kv.first);
        }
        return result;
    }

private:
    void evaluate_parallel_(coopa::job::JobEngine& engine) {
        std::vector<std::function<void()>> chunks;
        for (size_t start = 0; start < animators_.size(); start += chunk_size_) {
            size_t end = std::min(start + chunk_size_, animators_.size());
            chunks.push_back([this, start, end]() {
                for (size_t i = start; i < end; ++i) {
                    animators_[i]->evaluate_(scratch_.data() + offsets_[i]);
                }
            });
        }
        coopa::job::JobHandle handle = engine.submit_jobs(chunks, k_animation_job_type);
        engine.wait_for(handle);
    }

    // Follows AssetManager::k_asset_io_job_type's precedent: any value >=
    // k_max_job_types (64) simply opts this job type out of thread
    // dedication and COOPA_JOB_DIAGNOSTICS counters, which is fine here —
    // this is a single fan-out-then-wait batch with zero inter-job
    // dependencies, so k_max_inline_dependencies doesn't come into play either.
    static constexpr JobType k_animation_job_type = 0x0A417000u;

    std::vector<Animator*> animators_;
    std::vector<size_t>    offsets_;
    std::vector<float>     scratch_;
    size_t                 parallel_threshold_ = 256;
    size_t                 chunk_size_         = 32;
    bool                   dirty_              = true;
};

/**
 * @brief Constructs an AnimationSystem and registers it at UpdatePhase::Animation.
 *
 * The one-line install an application needs; equivalent to
 * `scene.add_system(std::make_unique<AnimationSystem>(), UpdatePhase::Animation)`.
 *
 * @return Non-owning pointer to the installed system, for calling
 *         set_parallel_threshold()/find_conflicts() etc.
 */
inline AnimationSystem* install_animation_system(coopa::scene::Scene& scene) {
    auto sys = std::make_unique<AnimationSystem>();
    AnimationSystem* raw = sys.get();
    scene.add_system(std::move(sys), coopa::scene::UpdatePhase::Animation);
    return raw;
}

} // namespace anim
} // namespace coopa

#endif // COOPA_ANIMATION_ANIMATION_SYSTEM_H
