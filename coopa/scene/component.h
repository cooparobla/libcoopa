/**
 * @file component.h
 * @brief Abstract base class for all SceneObject components.
 *
 * Components encapsulate behavior attached to SceneObjects, analogous to Unity's
 * MonoBehaviour. Each component receives start() on scene load and update() every frame.
 */

#ifndef COOPA_SCENE_COMPONENT_H
#define COOPA_SCENE_COMPONENT_H

#include <string>

namespace coopa {
namespace scene {

// Forward declaration so components can access their owner.
class SceneObject;

/**
 * @class Component
 * @brief Abstract base class for all components attached to a SceneObject.
 *
 * Subclass and override start() and/or update() to add behavior.
 * The owner pointer is set automatically by SceneObject::add_component().
 */
class Component {
public:
    virtual ~Component() = default;

    /**
     * @brief Called once when the scene is loaded, after all objects are constructed.
     */
    virtual void start() {}

    /**
     * @brief Called every frame.
     * @param delta_time Time in seconds since the last frame.
     */
    virtual void update(float delta_time) { (void)delta_time; }

    /**
     * @brief Returns the type name string for this component.
     * @return Human-readable type name (e.g. "Transform", "MeshRenderer").
     */
    virtual std::string type_name() const = 0;

    SceneObject* owner = nullptr; /**< Non-owning pointer to the owning SceneObject. Set by SceneObject. */
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_COMPONENT_H
