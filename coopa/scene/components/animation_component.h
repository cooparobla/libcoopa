/**
 * @file animation_component.h
 * @brief Component for updating object position/transform with procedural or YAML-defined animations.
 */

#ifndef COOPA_SCENE_COMPONENTS_ANIMATION_COMPONENT_H
#define COOPA_SCENE_COMPONENTS_ANIMATION_COMPONENT_H

#include <coopa/scene/component.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/components/transform_component.h>
#include <caml/caml.h>
#include <fkYAML/node.hpp>
#include <glm/glm.hpp>
#include <string>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace coopa {
namespace scene {

/**
 * @enum AnimationType
 * @brief Supported animation trajectory types.
 */
enum class AnimationType {
    Orbit,    /**< Orbits horizontally around a center point or target object. */
    None
};

/**
 * @class AnimationComponent
 * @brief Animates a SceneObject's transform over time.
 *
 * Supports circular horizontal orbiting around a target object or center coordinate.
 * Animation configuration can be parsed from a YAML file in assets.
 */
class AnimationComponent : public Component {
public:
    AnimationType type = AnimationType::Orbit;
    std::string target_object = "";       /**< Name of target SceneObject to orbit around. */
    glm::vec3 center = glm::vec3(0.0f);   /**< Static center position if target_object is empty. */
    float radius = 2.7f;                  /**< Orbit radius. */
    float speed = 1.0f;                   /**< Orbit speed (radians per second). */
    float height = 1.0f;                  /**< Vertical offset/elevation (Z axis). */
    float initial_angle = 0.0f;           /**< Starting phase angle in radians. */
    float elapsed_time = 0.0f;            /**< Accumulated animation runtime in seconds. */
    std::string animation_file = "";      /**< Source YAML asset path. */

    AnimationComponent() = default;

    /**
     * @brief Loads animation parameters from a YAML file.
     * @param filepath Absolute or relative path to the YAML file.
     */
    void load_from_yaml(const std::string& filepath) {
        animation_file = filepath;
        try {
            caml::CAMLMap map = caml::CAMLMap::load_yaml(filepath);
            const auto& root = map.get_raw_node();
            parse_node(root);
        } catch (const std::exception& e) {
            std::cerr << "[AnimationComponent] Failed to load animation file '"
                      << filepath << "': " << e.what() << std::endl;
        }
    }

    /**
     * @brief Parses configuration options from an fkyaml node.
     * @param node The fkyaml node containing parameters.
     */
    void parse_node(const fkyaml::node& node) {
        if (node.contains("type")) {
            std::string t_str = node.at("type").get_value<std::string>();
            if (t_str == "orbit" || t_str == "Orbit" || t_str == "circular" || t_str == "Circular") {
                type = AnimationType::Orbit;
            }
        }
        if (node.contains("target_object")) {
            target_object = node.at("target_object").get_value<std::string>();
        }
        if (node.contains("center")) {
            const auto& c = node.at("center");
            center.x = c.contains("x") ? c.at("x").get_value<float>() : center.x;
            center.y = c.contains("y") ? c.at("y").get_value<float>() : center.y;
            center.z = c.contains("z") ? c.at("z").get_value<float>() : center.z;
        }
        if (node.contains("radius")) {
            radius = node.at("radius").get_value<float>();
        }
        if (node.contains("speed")) {
            speed = node.at("speed").get_value<float>();
        }
        if (node.contains("height")) {
            height = node.at("height").get_value<float>();
        }
        if (node.contains("initial_angle")) {
            initial_angle = node.at("initial_angle").get_value<float>();
        }
    }

    void start() override {
        // Initialization if required on scene start
    }

    void update(float delta_time) override {
        if (!owner) return;
        auto* transform_comp = owner->get_transform();
        if (!transform_comp) return;

        elapsed_time += delta_time;

        if (type == AnimationType::Orbit) {
            glm::vec3 pivot_center = center;

            if (!target_object.empty()) {
                SceneObject* target_node = find_target_object(owner, target_object);
                if (target_node && target_node->get_transform()) {
                    pivot_center = target_node->get_transform()->transform().position();
                }
            }

            float angle = initial_angle + speed * elapsed_time;
            float x = pivot_center.x + radius * std::cos(angle);
            float y = pivot_center.y + radius * std::sin(angle);
            float z = pivot_center.z + height;

            transform_comp->transform().set_position(glm::vec3(x, y, z));
        }
    }

    std::string type_name() const override {
        return "Animation";
    }

private:
    static SceneObject* find_target_object(SceneObject* current, const std::string& name) {
        if (!current) return nullptr;
        SceneObject* root = current;
        while (root->parent()) {
            root = root->parent();
        }

        SceneObject* found = nullptr;
        root->for_each_recursive([&](const SceneObject& obj) {
            if (!found && obj.name() == name) {
                found = const_cast<SceneObject*>(&obj);
            }
        });
        return found;
    }
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_COMPONENTS_ANIMATION_COMPONENT_H
