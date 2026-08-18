/**
 * @file scene_object.h
 * @brief Named scene node with a component list and child hierarchy.
 *
 * SceneObject is the "GameObject" of the scene graph. It carries no
 * assumptions about which component types exist — those are supplied by
 * callers (e.g. SceneLoader adds a TransformComponent to every parsed
 * object, but a programmatically-built SceneObject starts with none).
 * Components are owned via unique_ptr; children are also owned.
 */

#ifndef COOPA_SCENE_SCENE_OBJECT_H
#define COOPA_SCENE_SCENE_OBJECT_H

#include <coopa/scene/component.h>
#include <coopa/scene/components/transform_component.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <typeindex>
#include <stdexcept>

namespace coopa {
namespace scene {

/**
 * @class SceneObject
 * @brief A named node in the scene hierarchy, owning components and child objects.
 *
 * Usage:
 * @code
 * auto obj = std::make_unique<SceneObject>("Cube");
 * auto* tc = obj->add_component<TransformComponent>();
 * obj->add_child(std::make_unique<SceneObject>("Child"));
 * @endcode
 */
class SceneObject {
public:
    /**
     * @brief Constructs a SceneObject with the given name.
     *
     * Carries no components until add_component()/attach_component() is
     * called — SceneLoader adds a TransformComponent to every object it
     * parses, but that is a loader convention, not a guarantee of this class.
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
     * Avoids template syntax in generic pipeline code. TransformComponent is
     * the one component type the scene system itself defines, so this
     * accessor is safe to keep here without pulling in any external module.
     *
     * @return Pointer to the TransformComponent, or nullptr if missing.
     */
    TransformComponent* get_transform() const {
        return get_component<TransformComponent>();
    }

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
     *
     * Only the SceneObject ownership link is established here — a child's
     * TransformComponent (if any) is NOT auto-linked to this object's
     * Transform, because SceneLoader::parse_object_ already performs that
     * linking explicitly (via TransformComponent::set_parent_transform)
     * before calling add_child, and doing it again here would register the
     * child's Transform twice in the parent's dirty-propagation list.
     * Callers building a hierarchy by hand must link transforms themselves;
     * set_parent() below does this automatically for reparenting.
     *
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
     * pointer is cleared, and its TransformComponent (if any) is unlinked
     * from this object's Transform child-list so no dangling pointer is left
     * behind for dirty-flag propagation.
     *
     * @param child Pointer to the direct child to detach.
     * @return Owning pointer to the detached child, or nullptr if not found.
     */
    std::unique_ptr<SceneObject> detach_child(SceneObject* child) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [child](const std::unique_ptr<SceneObject>& c) { return c.get() == child; });
        if (it == children_.end()) return nullptr;
        std::unique_ptr<SceneObject> detached = std::move(*it);
        if (auto* child_tc = detached->get_transform()) {
            if (auto* self_tc = get_transform()) {
                self_tc->transform().remove_child(&child_tc->transform());
            }
            child_tc->transform().set_parent(nullptr);
        }
        children_.erase(it);
        detached->parent_ = nullptr;
        return detached;
    }

    /**
     * @brief Reparents this object under new_parent, preserving the subtree.
     *
     * Detaches this object from its current parent (if any) and adds it to
     * new_parent's children. No-op if new_parent is already the current
     * parent. If this object carries a TransformComponent, its Transform is
     * also re-linked under new_parent's TransformComponent (or unlinked to
     * root if new_parent has none) — detach_child() already unlinks it from
     * the old Transform parent, so this only needs to (re)link the new side.
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
        if (auto* self_tc = self_owned->get_transform()) {
            auto* new_parent_tc = new_parent->get_transform();
            self_tc->set_parent_transform(new_parent_tc ? &new_parent_tc->transform() : nullptr);
        }
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

    /**
     * @brief Depth-first search for a descendant with the given name.
     *
     * Excludes this object itself — only children and further descendants
     * are considered. Useful for resolving cross-references stored as names
     * in scene YAML (e.g. ScrollRect::content) once the whole tree exists.
     *
     * @param name Name to search for.
     * @return Non-owning pointer to the first matching descendant, or nullptr.
     */
    SceneObject* find_descendant(const std::string& name) const {
        for (const auto& child : children_) {
            if (child->name() == name) return child.get();
            if (SceneObject* found = child->find_descendant(name)) return found;
        }
        return nullptr;
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

#endif // COOPA_SCENE_SCENE_OBJECT_H
