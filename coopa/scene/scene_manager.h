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
#include <gfxcoopa/core/device.h>
#include <gfxcoopa/memory/allocator.h>
#include <gfxcoopa/command/command_pool.h>
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
 * SceneManager mgr(device, allocator, cmd_pool);
 * mgr.load_scene("assets/scenes/cube/scene.yaml");
 * // Per frame:
 * mgr.update(delta_time);
 * auto& scene = mgr.get_active_scene();
 * @endcode
 */
class SceneManager {
public:
    /**
     * @brief Constructs the manager with the Vulkan resources needed for mesh loading.
     * @param device    Vulkan logical device.
     * @param allocator VMA allocator.
     * @param cmd_pool  Command pool for staging transfers.
     */
    SceneManager(coopa::gfx::core::Device&         device,
                 coopa::gfx::memory::Allocator&    allocator,
                 coopa::gfx::command::CommandPool& cmd_pool)
        : device_(device), allocator_(allocator), cmd_pool_(cmd_pool)
    {}

    /**
     * @brief Loads a scene from the given path (replaces any previously active scene).
     *
     * Accepts both .yaml and .caml files.
     *
     * @param path Path to the scene file.
     * @throws std::runtime_error on load failure.
     */
    void load_scene(const std::string& path) {
        active_scene_ = std::make_unique<Scene>(
            SceneLoader::load(path, device_, allocator_, cmd_pool_)
        );
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

private:
    coopa::gfx::core::Device&         device_;    /**< Vulkan device (not owned). */
    coopa::gfx::memory::Allocator&    allocator_; /**< VMA allocator (not owned). */
    coopa::gfx::command::CommandPool& cmd_pool_;  /**< Command pool (not owned). */
    std::unique_ptr<Scene>            active_scene_; /**< Owned active scene. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_MANAGER_H
