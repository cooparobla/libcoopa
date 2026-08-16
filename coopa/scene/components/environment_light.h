/**
 * @file environment_light.h
 * @brief Scene component defining global sky/ambient background lighting parameters.
 */

#ifndef COOPA_SCENE_COMPONENTS_ENVIRONMENT_LIGHT_H
#define COOPA_SCENE_COMPONENTS_ENVIRONMENT_LIGHT_H

#include <coopa/scene/component.h>
#include <glm/glm.hpp>
#include <string>

namespace coopa {
namespace scene {

/// Global environment/sky lighting fallback.
class EnvironmentLightComponent : public Component {
public:
    EnvironmentLightComponent() = default;
    std::string type_name() const override { return "EnvironmentLightComponent"; }
    void update(float /*dt*/) override {}

    glm::vec3 sky_color     = glm::vec3(0.5f, 0.7f, 1.0f);
    glm::vec3 ground_color  = glm::vec3(0.1f, 0.08f, 0.05f);
    float     sky_intensity = 1.0f;
    std::string hdri_path   = "";
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_COMPONENTS_ENVIRONMENT_LIGHT_H
