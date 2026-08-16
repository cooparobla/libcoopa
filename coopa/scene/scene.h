/**
 * @file scene.h
 * @brief Root of the scene hierarchy, owning root SceneObjects.
 *
 * A Scene contains the named root objects and tracks the active camera.
 * The update() method processes all root objects (and their children recursively)
 * and can be dispatched as a job on a worker thread via libcoopa's JobEngine.
 */

#ifndef COOPA_SCENE_SCENE_H
#define COOPA_SCENE_SCENE_H

#include <coopa/scene/scene_object.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace coopa {
namespace scene {

// Forward declarations.
class CameraComponent;
class DirectionalLightComponent;
class PointLightComponent;
class MeshRenderer;
class GiProbeVolumeComponent;
class ReflectionProbeComponent;

/**
 * @class Scene
 * @brief Owns the root SceneObjects and tracks the active camera.
 *
 * Constructed by SceneLoader. update() walks the full hierarchy.
 * get_renderable_objects() provides a flat list of objects with MeshRenderer
 * components for the render pipeline to consume.
 */
class Scene {
public:
    /**
     * @brief Constructs an empty scene with the given name.
     * @param name Scene name (e.g. from the YAML scene_name field).
     */
    explicit Scene(std::string name = "Scene")
        : name_(std::move(name)), active_camera_(nullptr), active_light_(nullptr)
    {}

    // Non-copyable, movable.
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    // --- Accessors ---

    /** @brief Returns the scene name. */
    const std::string& name() const { return name_; }

    /**
     * @brief Adds a root SceneObject (takes ownership).
     * @param obj The root object to add.
     * @return Raw pointer to the added object.
     */
    SceneObject* add_root_object(std::unique_ptr<SceneObject> obj) {
        SceneObject* raw = obj.get();
        root_objects_.push_back(std::move(obj));
        return raw;
    }

    /** @brief Returns the list of root SceneObjects (read-only). */
    const std::vector<std::unique_ptr<SceneObject>>& root_objects() const {
        return root_objects_;
    }

    /** @brief Returns the list of root SceneObjects (mutable). */
    std::vector<std::unique_ptr<SceneObject>>& root_objects() {
        return root_objects_;
    }

    /**
     * @brief Sets the active camera component.
     * @param cam Non-owning pointer to a CameraComponent (owned by a SceneObject).
     */
    void set_active_camera(CameraComponent* cam) { active_camera_ = cam; }

    /**
     * @brief Returns the active camera component, or nullptr if none has been set.
     */
    CameraComponent* active_camera() const { return active_camera_; }

    /**
     * @brief Sets the active directional light component.
     * @param light Non-owning pointer (owned by a SceneObject).
     */
    void set_active_light(DirectionalLightComponent* light) { active_light_ = light; }

    /**
     * @brief Returns the active directional light, or nullptr if the scene has none.
     */
    DirectionalLightComponent* active_light() const { return active_light_; }

    // --- Lifecycle ---

    /**
     * @brief Calls start() on all root objects (recursively).
     *
     * Should be called once after the scene is fully loaded.
     */
    void start() {
        for (auto& obj : root_objects_) obj->start();
    }

    /**
     * @brief Calls update(dt) on all root objects (recursively).
     *
     * Safe to call from a worker thread as long as the render thread is not
     * simultaneously reading component state.
     *
     * @param delta_time Frame delta time in seconds.
     */
    void update(float delta_time) {
        for (auto& obj : root_objects_) obj->update(delta_time);
    }

    // --- Queries ---

    /**
     * @brief Returns a flat list of all active PointLightComponents in the scene hierarchy.
     * @return Vector of non-owning PointLightComponent pointers.
     */
    std::vector<PointLightComponent*> get_point_lights() const {
        std::vector<PointLightComponent*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (auto* pl = const_cast<SceneObject&>(obj).get_component<PointLightComponent>()) {
                    result.push_back(pl);
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns a flat list of all active GiProbeVolumeComponents in the scene hierarchy.
     */
    std::vector<GiProbeVolumeComponent*> get_gi_probe_volumes() const {
        std::vector<GiProbeVolumeComponent*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (auto* gv = const_cast<SceneObject&>(obj).get_component<GiProbeVolumeComponent>()) {
                    result.push_back(gv);
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns a flat list of all active ReflectionProbeComponents in the scene hierarchy.
     */
    std::vector<ReflectionProbeComponent*> get_reflection_probes() const {
        std::vector<ReflectionProbeComponent*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (auto* rp = const_cast<SceneObject&>(obj).get_component<ReflectionProbeComponent>()) {
                    result.push_back(rp);
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns a flat list of all SceneObjects that have a MeshRenderer component.
     *
     * Traverses the full hierarchy depth-first.
     *
     * @return Vector of non-owning SceneObject pointers.
     */
    std::vector<SceneObject*> get_renderable_objects() const {
        std::vector<SceneObject*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (obj.get_component<MeshRenderer>()) {
                    result.push_back(const_cast<SceneObject*>(&obj));
                }
            });
        }
        return result;
    }

    /**
     * @brief Finds the first SceneObject with the given name (depth-first).
     * @param name Name to search for.
     * @return Non-owning pointer to the found object, or nullptr.
     */
    SceneObject* find_object(const std::string& name) const {
        for (const auto& root : root_objects_) {
            SceneObject* found = nullptr;
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!found && obj.name() == name) {
                    found = const_cast<SceneObject*>(&obj);
                }
            });
            if (found) return found;
        }
        return nullptr;
    }

private:
    std::string                               name_;           /**< Scene name. */
    std::vector<std::unique_ptr<SceneObject>> root_objects_;   /**< Owned root objects. */
    CameraComponent*                          active_camera_;  /**< Non-owning active camera. */
    DirectionalLightComponent*                active_light_;   /**< Non-owning active directional light. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_H
