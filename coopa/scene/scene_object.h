/**
 * @file scene_object.h
 * @brief Named scene node with a component list and child hierarchy.
 *
 * SceneObject is the "GameObject" of the scene graph. Each object always has
 * a TransformComponent as its first component (added at construction).
 * Components are owned via unique_ptr; children are also owned.
 */

#ifndef COOPA_SCENE_SCENE_OBJECT_H
#define COOPA_SCENE_SCENE_OBJECT_H

#include <coopa/scene/component.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <typeindex>
#include <stdexcept>

namespace coopa {
namespace scene {

// Forward declarations to avoid circular includes.
// Implementations are provided inline after the class definition.
class TransformComponent;
class MeshRenderer;
class AnimationComponent;

/**
 * @class SceneObject
 * @brief A named node in the scene hierarchy, owning components and child objects.
 *
 * Usage:
 * @code
 * auto obj = std::make_unique<SceneObject>("Cube");
 * auto* mesh = obj->add_component<MeshRenderer>();
 * obj->add_child(std::make_unique<SceneObject>("Child"));
 * @endcode
 */
class SceneObject {
public:
    /**
     * @brief Constructs a SceneObject with the given name.
     *
     * A TransformComponent is always added first (added by SceneLoader after construction
     * to allow forward-declaration of TransformComponent).
     *
     * @param name The object's name.
     * @param active Whether the object is active on creation.
     */
    explicit SceneObject(std::string name, bool active = true)
        : name_(std::move(name)), active_(active), parent_(nullptr)
    {}

    // --- Identity ---

    /** @brief Returns the object name. */
    const std::string& name() const { return name_; }

    /** @brief Returns whether this object is active. */
    bool active() const { return active_; }

    /** @brief Sets the active state. */
    void set_active(bool active) { active_ = active; }

    /** @brief Returns the parent SceneObject (non-owning, may be nullptr). */
    SceneObject* parent() const { return parent_; }

    // --- Components ---

    /**
     * @brief Constructs and attaches a component of type T.
     *
     * Sets the component's owner pointer to this object.
     *
     * @tparam T Component subclass to create.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     * @return Raw pointer to the newly added component.
     */
    template<typename T, typename... Args>
    T* add_component(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->owner = this;
        T* raw = comp.get();
        components_.push_back(std::move(comp));
        return raw;
    }

    /**
     * @brief Returns the first component of type T, or nullptr if not found.
     * @tparam T Component type to search for.
     * @return Pointer to the component, or nullptr.
     */
    template<typename T>
    T* get_component() const {
        for (const auto& comp : components_) {
            if (T* ptr = dynamic_cast<T*>(comp.get())) {
                return ptr;
            }
        }
        return nullptr;
    }

    /**
     * @brief Returns every component of type T found on this object or any descendant.
     * @tparam T Component type to search for.
     * @return Pointers to all matching components, in pre-order.
     */
    template<typename T>
    std::vector<T*> get_components_in_children() const {
        std::vector<T*> result;
        for_each_recursive([&result](const SceneObject& obj) {
            if (T* ptr = obj.get_component<T>()) {
                result.push_back(ptr);
            }
        });
        return result;
    }

    /**
     * @brief Removes and destroys the given component if owned by this object.
     * @param comp Pointer to the component to remove.
     * @return True if a matching component was found and removed.
     */
    bool remove_component(Component* comp) {
        auto it = std::find_if(components_.begin(), components_.end(),
            [comp](const std::unique_ptr<Component>& c) { return c.get() == comp; });
        if (it == components_.end()) return false;
        (*it)->owner = nullptr;
        components_.erase(it);
        return true;
    }

    /**
     * @brief Removes and destroys the first component of type T.
     * @tparam T Component type to remove.
     * @return True if a matching component was found and removed.
     */
    template<typename T>
    bool remove_component() {
        if (T* ptr = get_component<T>()) {
            return remove_component(ptr);
        }
        return false;
    }

    /**
     * @brief Returns the TransformComponent (convenience alias for get_component<TransformComponent>).
     *
     * Avoids template syntax in generic pipeline code.
     *
     * @return Pointer to the TransformComponent, or nullptr if missing.
     */
    TransformComponent* get_transform() const;

    /**
     * @brief Returns the MeshRenderer component (convenience alias).
     *
     * Avoids template syntax in generic pipeline code.
     *
     * @return Pointer to the MeshRenderer, or nullptr if missing.
     */
    MeshRenderer* get_mesh_renderer() const;

    /**
     * @brief Returns the AnimationComponent (convenience alias).
     * @return Pointer to the AnimationComponent, or nullptr if missing.
     */
    AnimationComponent* get_animation() const;

    /**
     * @brief Directly attaches a pre-constructed component (takes ownership).
     *
     * Sets the component's owner pointer to this object.
     *
     * @param comp Owning unique_ptr to the component.
     */
    void attach_component(std::unique_ptr<Component> comp) {
        comp->owner = this;
        components_.push_back(std::move(comp));
    }

    /** @brief Returns all components (read-only). */
    const std::vector<std::unique_ptr<Component>>& components() const { return components_; }

    // --- Hierarchy ---

    /**
     * @brief Adds a child SceneObject (takes ownership).
     * @param child The child to add. Its parent pointer is set to this.
     * @return Raw pointer to the child.
     */
    SceneObject* add_child(std::unique_ptr<SceneObject> child) {
        child->parent_ = this;
        SceneObject* raw = child.get();
        children_.push_back(std::move(child));
        return raw;
    }

    /**
     * @brief Detaches a child from this object without destroying it.
     *
     * The returned unique_ptr owns the detached subtree; the child's parent
     * pointer is cleared. Caller is responsible for re-parenting or discarding it.
     *
     * @param child Pointer to the direct child to detach.
     * @return Owning pointer to the detached child, or nullptr if not found.
     */
    std::unique_ptr<SceneObject> detach_child(SceneObject* child) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [child](const std::unique_ptr<SceneObject>& c) { return c.get() == child; });
        if (it == children_.end()) return nullptr;
        std::unique_ptr<SceneObject> detached = std::move(*it);
        children_.erase(it);
        detached->parent_ = nullptr;
        return detached;
    }

    /**
     * @brief Reparents this object under new_parent, preserving the subtree.
     *
     * Detaches this object from its current parent (if any) and adds it to
     * new_parent's children. No-op if new_parent is already the current parent.
     * Does not update any coopa::util::Transform parent links; callers that use
     * TransformComponent must call TransformComponent::set_parent_transform
     * separately to keep world-matrix propagation correct.
     *
     * @param new_parent The SceneObject to become the new parent. Must not be null.
     * @throws std::runtime_error if this object has no current SceneObject parent
     *         (e.g. it is a Scene root object) — ownership cannot be transferred
     *         without access to whatever container currently owns it.
     */
    void set_parent(SceneObject* new_parent) {
        if (new_parent == parent_ || new_parent == this) return;
        if (!parent_) {
            throw std::runtime_error(
                "SceneObject::set_parent: object has no SceneObject parent to detach from "
                "(likely a Scene root object); reparent it via Scene::root_objects() instead.");
        }
        std::unique_ptr<SceneObject> self_owned = parent_->detach_child(this);
        new_parent->add_child(std::move(self_owned));
    }

    /** @brief Returns the list of children (read-only). */
    const std::vector<std::unique_ptr<SceneObject>>& children() const { return children_; }
    /** @brief Returns the list of children (mutable). */
    std::vector<std::unique_ptr<SceneObject>>& children() { return children_; }

    /**
     * @brief Depth-first traversal of this object and all descendants.
     * @param fn Callable receiving a mutable reference to each SceneObject.
     */
    template<typename Fn>
    void for_each_recursive(Fn&& fn) {
        fn(*this);
        for (auto& child : children_) {
            child->for_each_recursive(std::forward<Fn>(fn));
        }
    }

    /**
     * @brief Depth-first traversal (const version).
     */
    template<typename Fn>
    void for_each_recursive(Fn&& fn) const {
        fn(*this);
        for (const auto& child : children_) {
            child->for_each_recursive(std::forward<Fn>(fn));
        }
    }

    // --- Lifecycle ---

    /**
     * @brief Calls start() on all components, then recurses to children.
     */
    void start() {
        if (!active_) return;
        for (auto& comp : components_) comp->start();
        for (auto& child : children_) child->start();
    }

    /**
     * @brief Calls update(dt) on all components, then recurses to active children.
     * @param delta_time Frame delta time in seconds.
     */
    void update(float delta_time) {
        if (!active_) return;
        for (auto& comp : components_) comp->update(delta_time);
        for (auto& child : children_) child->update(delta_time);
    }

private:
    std::string                              name_;       /**< Object name. */
    bool                                     active_;     /**< Whether updates/rendering are active. */
    SceneObject*                             parent_;     /**< Non-owning parent pointer. */
    std::vector<std::unique_ptr<Component>>  components_; /**< Owned components. */
    std::vector<std::unique_ptr<SceneObject>>children_;  /**< Owned children. */
};

} // namespace scene
} // namespace coopa

// Inline implementations of get_transform() and get_mesh_renderer().
// These are defined AFTER the class to allow use of incomplete types inside the class,
// and use the component headers which may themselves include scene_object.h (guarded by include guards).
#include <coopa/scene/components/transform_component.h>
#include <coopa/scene/components/mesh_renderer.h>
#include <coopa/scene/components/animation_component.h>

namespace coopa {
namespace scene {

inline TransformComponent* SceneObject::get_transform() const {
    return get_component<TransformComponent>();
}

inline MeshRenderer* SceneObject::get_mesh_renderer() const {
    return get_component<MeshRenderer>();
}

inline AnimationComponent* SceneObject::get_animation() const {
    return get_component<AnimationComponent>();
}

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_OBJECT_H
