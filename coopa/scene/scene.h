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
#include <coopa/event/event_bus.h>
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

    // Non-copyable, movable. Move operations re-stamp every component's
    // Component::scene back-pointer to the NEW Scene address (see
    // restamp_scene_pointers_()) — without this, a Scene moved after start()
    // has run (e.g. SceneLoader::load()'s `return scene;`, then moved again
    // into SceneManager's unique_ptr<Scene>) would leave every component
    // pointing at a dangling, already-destroyed Scene.
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Scene(Scene&& other) noexcept
        : name_(std::move(other.name_)),
          root_objects_(std::move(other.root_objects_)),
          events_(std::move(other.events_))
    {
        restamp_scene_pointers_();
    }

    Scene& operator=(Scene&& other) noexcept {
        if (this == &other) return *this;
        name_         = std::move(other.name_);
        root_objects_ = std::move(other.root_objects_);
        events_       = std::move(other.events_);
        restamp_scene_pointers_();
        return *this;
    }

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
     * Should be called once after the scene is fully loaded. Also stamps
     * every component's Component::scene back-pointer to this Scene (just
     * before that component's own start() runs), so start()/update()/
     * late_update() can all reach scene->events() from within a component.
     */
    void start() {
        restamp_scene_pointers_();
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

    /**
     * @brief Calls late_update(dt) on all root objects (recursively).
     *
     * Call once per frame, after update(dt) — see Component::late_update()
     * for why the two are separate passes.
     *
     * @param delta_time Frame delta time in seconds.
     */
    void late_update(float delta_time) {
        for (auto& obj : root_objects_) obj->late_update(delta_time);
    }

    /**
     * @brief Stamps Component::scene on every component in obj's subtree,
     *        without calling start() on any of them.
     *
     * For attaching a subtree to this Scene after start() has already run
     * once (e.g. dynamically spawning a new SceneObject mid-game) — the
     * ordinary path (a subtree present at load time) is already covered by
     * start(). Idempotent; safe to call on a subtree that already belongs to
     * this Scene.
     *
     * @param obj Root of the subtree to adopt. Must already be reachable
     *            from this Scene's root_objects() (e.g. via add_root_object()
     *            or SceneObject::add_child()) — this only stamps the
     *            back-pointer, it does not change ownership.
     */
    void adopt(SceneObject& obj) {
        obj.for_each_recursive([this](SceneObject& o) {
            for (auto& comp : o.components()) comp->scene = this;
        });
    }

    /** @brief The scene-wide named EventBus — see coopa/event/event_bus.h. */
    coopa::event::EventBus& events() { return events_; }

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
    /**
     * @brief Stamps Component::scene = this on every component in the hierarchy.
     *
     * Regardless of active() — an initially-inactive object's components
     * should still be able to reach scene->events() once reactivated later
     * via set_active(true). Shared by start() and the move operations (a
     * moved-into Scene has a different address than the one that may have
     * already run start() before the move — see the move ctor/assignment doc).
     */
    void restamp_scene_pointers_() {
        for (auto& root : root_objects_) {
            root->for_each_recursive([this](SceneObject& obj) {
                for (auto& comp : obj.components()) comp->scene = this;
            });
        }
    }

    std::string                               name_;         /**< Scene name. */
    std::vector<std::unique_ptr<SceneObject>> root_objects_; /**< Owned root objects. */
    coopa::event::EventBus                    events_;       /**< Scene-wide named signal bus. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_H
