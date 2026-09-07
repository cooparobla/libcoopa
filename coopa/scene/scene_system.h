/**
 * @file scene_system.h
 * @brief Ordered per-frame system pipeline that Scene::update()/late_update() drive.
 *
 * A Scene no longer just walks Component::update()/late_update() directly —
 * it runs a small ordered list of ISceneSystem instances, of which the
 * Component walks are just the two built-in members (BehaviourSystem and
 * LateBehaviourSystem). This gives future subsystems (animation now, physics
 * later) a defined point in the frame to run at, and gives the Scene a single
 * place to own a JobEngine's frame boundary — see FrameContext::jobs below
 * for why that ownership matters.
 */

#ifndef COOPA_SCENE_SCENE_SYSTEM_H
#define COOPA_SCENE_SCENE_SYSTEM_H

#include <cstdint>

// Forward declaration only — scene_system.h (and therefore scene.h) must not
// include coopa/job/engine.h. coopa/scene/README.md restricts the module to
// vendored fkYAML/glm plus libcoopa's own coopa/event; a raw JobEngine* here
// keeps that true and keeps engine.h's <thread>/<condition_variable>/
// <iostream> out of every scene.h translation unit. A system that actually
// submits jobs includes coopa/job/engine.h itself.
namespace coopa {
namespace job {
class JobEngine;
} // namespace job
} // namespace coopa

namespace coopa {
namespace scene {

class Scene;

/**
 * @brief Ordering slots for scene systems, spaced by 100 so a consumer can
 *        insert a new system between built-ins without renumbering anything.
 *
 * Scene::update() runs every system with order < LateBehaviour;
 * Scene::late_update() runs every system with order >= LateBehaviour. That
 * split is what keeps both existing public entry points behaving exactly as
 * they did before this pipeline existed (see Scene::update()'s doc for the
 * full compatibility argument) while still landing Animation before the
 * built-in late_update() walk, matching Unity's Update -> animation ->
 * LateUpdate order.
 */
enum class UpdatePhase : int {
    Physics       = 100, ///< Reserved. No implementation ships with this change.
    Behaviour     = 200, ///< Built-in: the recursive Component::update() walk.
    Animation     = 300, ///< coopa::anim::AnimationSystem: evaluate + apply.
    LateBehaviour = 400, ///< Built-in: the recursive Component::late_update() walk.
};

/**
 * @struct FrameContext
 * @brief Per-frame inputs handed to every ISceneSystem::execute() call.
 */
struct FrameContext {
    float    delta_time  = 0.0f; /**< Seconds since the last frame. */
    uint64_t frame_index = 0;    /**< Monotonically increasing frame counter, from Scene::frame_index(). */

    /**
     * @brief The Scene's frame JobEngine, or nullptr.
     *
     * nullptr means no engine was ever installed via Scene::set_job_engine()
     * — every system must then do its work inline on the calling thread. This
     * is the default, so an application that never touches set_job_engine()
     * spawns no threads and behaves exactly as it did before phases existed.
     *
     * A system MAY call create_handle()/submit()/submit_jobs()/wait_for() on
     * this engine. A system MUST NEVER call begin_frame()/end_frame() on it:
     * Scene owns the frame boundary (see Scene::set_job_engine()'s doc) —
     * CounterPool is an untagged bump allocator, so a second begin_frame()
     * from inside a system would silently invalidate every other subsystem's
     * outstanding handles.
     */
    coopa::job::JobEngine* jobs = nullptr;
};

/**
 * @class ISceneSystem
 * @brief A unit of per-frame scene work, run at a fixed point in the phase order.
 *
 * Register an instance with Scene::add_system(). Systems are not copyable or
 * default-constructed by the Scene — it only ever holds the unique_ptr it was
 * handed.
 */
class ISceneSystem {
public:
    virtual ~ISceneSystem() = default;

    /** @brief Called once when the system is registered via Scene::add_system(). */
    virtual void on_attach(Scene& scene) { (void)scene; }

    /** @brief Called once when the system is removed via Scene::remove_system(). */
    virtual void on_detach(Scene& scene) { (void)scene; }

    /**
     * @brief Does this system's per-frame work.
     *
     * Called with a live Scene& every time — a system must not cache the
     * reference it receives in on_attach() and use it later instead, since
     * nothing guarantees the same Scene address across a move.
     */
    virtual void execute(Scene& scene, const FrameContext& ctx) = 0;

    /** @brief Stable name used by Scene::find_system()/remove_system() and logging. */
    virtual const char* system_name() const = 0;
};

/**
 * @class BehaviourSystem
 * @brief Built-in UpdatePhase::Behaviour step: the recursive Component::update() walk.
 *
 * Auto-installed by every Scene at construction. Remove it via
 * Scene::remove_system("Behaviour") if an application wants to replace the
 * built-in walk with something else.
 */
class BehaviourSystem : public ISceneSystem {
public:
    void execute(Scene& scene, const FrameContext& ctx) override;
    const char* system_name() const override { return "Behaviour"; }
};

/**
 * @class LateBehaviourSystem
 * @brief Built-in UpdatePhase::LateBehaviour step: the recursive Component::late_update() walk.
 *
 * Auto-installed by every Scene at construction.
 */
class LateBehaviourSystem : public ISceneSystem {
public:
    void execute(Scene& scene, const FrameContext& ctx) override;
    const char* system_name() const override { return "LateBehaviour"; }
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_SYSTEM_H
