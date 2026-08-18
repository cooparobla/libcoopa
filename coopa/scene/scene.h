/**
 * @file scene.h
 * @brief Root of the scene hierarchy, owning root SceneObjects.
 *
 * A Scene contains the named root objects. It carries no knowledge of any
 * particular component type (camera, light, mesh renderer, ...) — those are
 * defined outside libcoopa (e.g. gfxcoopa's engine::components), and are
 * reached generically via get_components<T>() / find_first_component<T>().
 * update() processes all root objects (and their children recursively) and
 * can be dispatched as a job on a worker thread via libcoopa's JobEngine.
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

/**
 * @class Scene
 * @brief Owns the root SceneObjects and provides generic hierarchy-wide queries.
 *
 * Constructed by SceneLoader (or built up manually via add_root_object()).
 * update() walks the full hierarchy. get_components<T>() / find_objects_with<T>()
 * / find_first_component<T>() give the render pipeline (or any other system)
 * a flat view over whichever component types it cares about, without Scene
 * needing to know those types itself.
 */
class Scene {
public:
    /**
     * @brief Constructs an empty scene with the given name.
     * @param name Scene name (e.g. from the YAML scene_name field).
     */
    explicit Scene(std::string name = "Scene")
        : name_(std::move(name))
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

    // --- Generic queries ---

    /**
     * @brief Returns a flat list of every active component of type T in the scene hierarchy.
     * @tparam T Component type to search for.
     * @return Vector of non-owning T* pointers, in pre-order.
     */
    template<typename T>
    std::vector<T*> get_components() const {
        std::vector<T*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (auto* c = const_cast<SceneObject&>(obj).template get_component<T>()) {
                    result.push_back(c);
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns every active SceneObject that carries a component of type T.
     * @tparam T Component type to search for.
     * @return Vector of non-owning SceneObject pointers, in pre-order.
     */
    template<typename T>
    std::vector<SceneObject*> find_objects_with() const {
        std::vector<SceneObject*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (obj.template get_component<T>()) {
                    result.push_back(const_cast<SceneObject*>(&obj));
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns the first active component of type T found in the hierarchy (pre-order).
     * @tparam T Component type to search for.
     * @return Non-owning pointer to the first match, or nullptr.
     */
    template<typename T>
    T* find_first_component() const {
        T* found = nullptr;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!found && obj.active()) {
                    found = const_cast<SceneObject&>(obj).template get_component<T>();
                }
            });
            if (found) return found;
        }
        return nullptr;
    }

    /**
     * @brief Finds the first SceneObject with the given name (depth-first).
     * @param name Name to search for.
     * @return Non-owning pointer to the found object, or nullptr.
     */
    SceneObject* find_object(const std::string& name) const {
        for (const auto& root : root_objects_) {
            if (root->name() == name) return root.get();
            if (SceneObject* found = root->find_descendant(name)) return found;
        }
        return nullptr;
    }

private:
    std::string                               name_;         /**< Scene name. */
    std::vector<std::unique_ptr<SceneObject>> root_objects_; /**< Owned root objects. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_H
