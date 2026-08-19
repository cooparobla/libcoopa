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
     * Any component types beyond Transform/Animation must already have their
     * parsers registered (see SceneLoader::register_component_parser) — this
     * class holds no rendering-specific resources of its own.
     *
     * @param path Path to the scene file.
     * @throws std::runtime_error on load failure.
     */
    void load_scene(const std::string& path) {
        active_scene_ = std::make_unique<Scene>(SceneLoader::load(path));
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
    std::unique_ptr<Scene> active_scene_; /**< Owned active scene. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_MANAGER_H
