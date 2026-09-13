/**
 * @file animation_yaml.h
 * @brief Wires AnimationClip and Animator into the asset system and SceneLoader.
 */

#ifndef COOPA_ANIMATION_ANIMATION_YAML_H
#define COOPA_ANIMATION_ANIMATION_YAML_H

#include <coopa/animation/animation_clip_loader.h>
#include <coopa/animation/animator.h>
#include <coopa/asset/asset_manager.h>
#include <coopa/scene/scene_loader.h>

#include <memory>
#include <string>

namespace coopa {
namespace anim {

/**
 * @brief Registers the AnimationClip asset loader and the "Animator" scene
 *        component parser against the given AssetManager.
 *
 * Call once at application startup, alongside any other
 * assets.register_loader<T>() calls (see coopa/asset/README.md's pattern).
 * The registered parser's closure captures `assets` by reference — call
 * coopa::scene::SceneLoader::clear_component_parsers() before `assets` is
 * destroyed to avoid a dangling capture surviving into a later
 * SceneLoader::load() call.
 *
 * The component tag is "Animator". Like every non-Transform component, it is
 * registered from outside libcoopa rather than built into SceneLoader — see
 * coopa/animation/README.md.
 *
 * @param assets The AssetManager clip states' `clip:` paths load through.
 */
inline void register_animation_components(coopa::asset::AssetManager& assets) {
    assets.register_loader<AnimationClip>(std::make_unique<AnimationClipLoader>());

    coopa::scene::SceneLoader::register_component_parser(
        "Animator",
        [&assets](const fkyaml::node& node, coopa::scene::SceneObject& obj,
                 const coopa::scene::SceneLoader::ParseContext& ctx) {
            auto* animator = obj.add_component<Animator>();

            if (node.contains("auto_play")) {
                animator->auto_play = node.at("auto_play").get_value<std::string>();
            }
            if (node.contains("speed")) {
                animator->set_speed(node.at("speed").get_value<float>());
            }
            if (node.contains("default_crossfade")) {
                animator->default_crossfade = node.at("default_crossfade").get_value<float>();
            }

            if (!node.contains("states")) return;
            for (const auto& state_node : node.at("states")) {
                if (!state_node.contains("name") || !state_node.contains("clip")) continue;
                std::string name = state_node.at("name").get_value<std::string>();
                std::string clip_path = state_node.at("clip").get_value<std::string>();

                // Unresolved relative path + scene_dir as base_dir, matching
                // the established loader-registration pattern (e.g.
                // gfxcoopa's MeshRenderer/material texture loading) rather
                // than pre-resolving via ParseContext::resolve() — this lets
                // AssetManager's own AssetSource search roots participate too.
                auto handle = assets.load_async<AnimationClip>(clip_path, ctx.scene_dir);
                AnimatorState* state = animator->add_state(name, std::move(handle));

                if (state_node.contains("wrap")) {
                    state->has_wrap_override = true;
                    state->wrap_override = parse_wrap_mode(state_node.at("wrap").get_value<std::string>());
                }
                if (state_node.contains("speed")) {
                    state->speed = state_node.at("speed").get_value<float>();
                }
            }
        });
}

} // namespace anim
} // namespace coopa

#endif // COOPA_ANIMATION_ANIMATION_YAML_H
