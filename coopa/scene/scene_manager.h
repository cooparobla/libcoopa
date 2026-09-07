/**
 * @file scene_manager.h
 * @brief Owns and manages the currently active Scene.
 *
 * Provides scene loading via SceneLoader and delegates per-frame updates
 * to the active scene. Future work: scene transitions, additive loading.
 */

#ifndef COOPA_SCENE_SCENE_MANAGER_H
#define COOPA_SCENE_SCENE_MANAGER_H

#include <coopa/scene/scene.h>
#include <coopa/scene/scene_loader.h>
#include <memory>
#include <string>
#include <stdexcept>

namespace coopa {
namespace scene {

/**
 * @class SceneManager
 * @brief Manages the lifecycle of the active scene.
 *
 * Usage:
 * @code
 * SceneManager mgr;
 * mgr.load_scene("assets/scenes/cube/scene.yaml");
 * // Per frame:
 * mgr.update(delta_time);
 * auto& scene = mgr.get_active_scene();
 * @endcode
 */
class SceneManager {
public:
    SceneManager() = default;

    /**
     * @brief Loads a scene from the given path (replaces any previously active scene).
     *
     * Any component type beyond Transform must already have its parser
     * registered (see SceneLoader::register_component_parser) — this class
     * holds no rendering-specific resources of its own.
     *
     * Re-applies whatever JobEngine was installed via set_job_engine() to the
     * newly loaded Scene, since this replaces active_scene_ wholesale.
     * Registered systems are NOT carried across — add_system() calls made on
     * a previous scene do not apply here; re-register systems after each
     * load_scene() call.
     *
     * @param path Path to the scene file.
     * @throws std::runtime_error on load failure.
     */
    void load_scene(const std::string& path) {
        active_scene_ = std::make_unique<Scene>(SceneLoader::load(path));
        active_scene_->set_job_engine(jobs_);
    }

    /**
     * @brief Installs the JobEngine to hand to every scene loaded from now on.
     *
     * Re-applied automatically inside load_scene(); call it again after a
     * later load_scene() only if the engine itself changes. See
     * Scene::set_job_engine() for the frame-boundary-ownership contract.
     */
    void set_job_engine(coopa::job::JobEngine* engine) {
        jobs_ = engine;
        if (active_scene_) active_scene_->set_job_engine(jobs_);
    }

    /** @brief Forwards to the active scene's add_system(). @throws std::runtime_error if no scene is loaded. */
    ISceneSystem* add_system(std::unique_ptr<ISceneSystem> system, UpdatePhase phase) {
        return get_active_scene().add_system(std::move(system), phase);
    }

    /** @brief Forwards to the active scene's remove_system(). @throws std::runtime_error if no scene is loaded. */
    bool remove_system(const std::string& system_name) {
        return get_active_scene().remove_system(system_name);
    }

    /** @brief Forwards to the active scene's find_system(). @throws std::runtime_error if no scene is loaded. */
    ISceneSystem* find_system(const std::string& system_name) const {
        return get_active_scene().find_system(system_name);
    }

    /**
     * @brief Returns true if a scene is currently loaded.
     */
    bool has_scene() const { return active_scene_ != nullptr; }

    /**
     * @brief Returns a reference to the active scene.
     * @throws std::runtime_error if no scene is loaded.
     */
    Scene& get_active_scene() {
        if (!active_scene_) throw std::runtime_error("[SceneManager] No active scene loaded.");
        return *active_scene_;
    }

    /**
     * @brief Returns a const reference to the active scene.
     */
    const Scene& get_active_scene() const {
        if (!active_scene_) throw std::runtime_error("[SceneManager] No active scene loaded.");
        return *active_scene_;
    }

    /**
     * @brief Calls update() on the active scene (if any).
     * @param delta_time Frame delta time in seconds.
     */
    void update(float delta_time) {
        if (active_scene_) active_scene_->update(delta_time);
    }

    /**
     * @brief Calls late_update() on the active scene (if any).
     *
     * Call once per frame, after update() — see Component::late_update()'s
     * doc for why the two are separate passes.
     * @param delta_time Frame delta time in seconds.
     */
    void late_update(float delta_time) {
        if (active_scene_) active_scene_->late_update(delta_time);
    }

private:
    std::unique_ptr<Scene> active_scene_;       /**< Owned active scene. */
    coopa::job::JobEngine* jobs_ = nullptr;     /**< Non-owning; re-applied to each newly loaded scene. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_MANAGER_H
