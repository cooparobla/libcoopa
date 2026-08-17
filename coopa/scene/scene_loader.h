/**
 * @file scene_loader.h
 * @brief Loads a Scene from a .yaml or .caml file using caml::CAMLMap.
 *
 * Both .yaml (CAMLMap::load_yaml) and .caml (CAMLMap::load_caml) are supported.
 * All parsing operates on fkyaml::node internally, providing a consistent code
 * path regardless of source format.
 *
 * The loader understands the Blender-exported scene format:
 *   format: blender
 *   scene:
 *     scene_name: <name>
 *     root_objects:
 *       - name: <str>
 *         active: <bool>
 *         components:
 *           - !Transform    { position, rotation, scale }
 *           - !MeshRenderer { mesh_path }
 *           - !Camera       { type, fov, ortho_scale, ... }
 *         children: [...]
 *
 * GPU mesh resources (Mesh) are created during loading and shared via
 * shared_ptr when multiple objects reference the same mesh path.
 */

#ifndef COOPA_SCENE_SCENE_LOADER_H
#define COOPA_SCENE_SCENE_LOADER_H

#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/components/transform_component.h>
#include <coopa/scene/components/mesh_renderer.h>
#include <coopa/scene/components/camera_component.h>
#include <coopa/scene/components/directional_light.h>
#include <coopa/scene/components/point_light.h>
#include <coopa/scene/components/gi_probe_volume.h>
#include <coopa/scene/components/reflection_probe.h>
#include <coopa/scene/components/environment_light.h>
#include <coopa/scene/components/animation_component.h>

#include <caml/caml.h>
#include <fkYAML/node.hpp>

#include <gfxcoopa/core/device.h>
#include <gfxcoopa/memory/allocator.h>
#include <gfxcoopa/engine/data/mesh.h>

#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <functional>

namespace coopa {
namespace scene {

/**
 * @class SceneLoader
 * @brief Parses .yaml or .caml scene files into a Scene with GPU mesh resources.
 *
 * Usage:
 * @code
 * auto scene = SceneLoader::load("assets/scenes/cube/scene.yaml", device, allocator);
 * @endcode
 */
class SceneLoader {
public:
    /**
     * @brief Loads a scene from a .yaml or .caml file.
     *
     * Detects the format from the file extension. Constructs the full Scene
     * hierarchy including GPU mesh uploads.
     *
     * @param path          Absolute or relative path to the scene file.
     * @param device        Vulkan logical device (for mesh buffer creation).
     * @param allocator     VMA allocator (for mesh memory allocation).
     * @param cmd_pool      Command pool for one-shot transfer commands.
     * @return Fully constructed Scene.
     * @throws std::runtime_error on parse errors or missing files.
     */
    static Scene load(const std::string&              path,
                      coopa::gfx::core::Device&       device,
                      coopa::gfx::memory::Allocator&  allocator,
                      coopa::gfx::command::CommandPool& cmd_pool)
    {
        // Determine scene directory for resolving relative mesh paths.
        std::filesystem::path scene_path(path);
        std::filesystem::path scene_dir = scene_path.parent_path();

        // Load via CAMLMap — same code path for both .yaml and .caml.
        caml::CAMLMap map = load_map_(path);
        const fkyaml::node& root = map.get_raw_node();

        // Mesh cache: logical path → shared GPU Mesh.
        std::unordered_map<std::string, std::shared_ptr<coopa::gfx::engine::data::Mesh>> mesh_cache;

        // Parse scene name.
        std::string scene_name = "Scene";
        if (root.contains("scene")) {
            const auto& scene_node = root.at("scene");
            if (scene_node.contains("scene_name")) {
                scene_name = scene_node.at("scene_name").get_value<std::string>();
            }
        }

        Scene scene(scene_name);

        // Parse root_objects.
        if (!root.contains("scene") || !root.at("scene").contains("root_objects")) {
            return scene; // Empty scene is valid.
        }

        const auto& root_objects_node = root.at("scene").at("root_objects");

        for (const auto& obj_node : root_objects_node) {
            auto obj = parse_object_(
                obj_node, nullptr,
                scene_dir.string(),
                device, allocator, cmd_pool,
                mesh_cache
            );

            // If a camera was found, register it.
            if (auto* cam = find_camera_(*obj)) {
                scene.set_active_camera(cam);
            }

            scene.add_root_object(std::move(obj));
        }

        // Register active light (first DirectionalLight found in the hierarchy).
        for (const auto& root : scene.root_objects()) {
            if (auto* light = find_light_(*root)) {
                scene.set_active_light(light);
                break;
            }
        }

        scene.start();
        return scene;
    }

    /**
     * @brief Signature for an externally-registered component parser.
     *
     * Receives the raw component YAML node and the SceneObject it should attach
     * to (already carrying its TransformComponent). Implementations should call
     * `obj.add_component<T>()` and populate it from `node`.
     */
    using ComponentParser = std::function<void(const fkyaml::node& node, SceneObject& obj)>;

    /**
     * @brief Registers a parser for a component YAML tag not built into SceneLoader.
     *
     * Lets libraries outside libcoopa (e.g. uicoopa) extend scene loading without
     * SceneLoader depending on them. Registering the same tag twice replaces the
     * previous parser. Built-in tags (e.g. "!Transform", "!MeshRenderer") cannot
     * be overridden — they are handled before the registry is consulted.
     *
     * @param tag YAML tag string as it appears in the scene file, e.g. "!RectTransform".
     * @param fn  Parser invoked when a component node carries this tag.
     */
    static void register_component_parser(const std::string& tag, ComponentParser fn) {
        parsers_()[tag] = std::move(fn);
    }

private:
    /**
     * @brief Function-local static registry of externally-registered component parsers.
     */
    static std::unordered_map<std::string, ComponentParser>& parsers_() {
        static std::unordered_map<std::string, ComponentParser> registry;
        return registry;
    }

    /**
     * @brief Loads a CAMLMap from a path, detecting .yaml vs .caml by extension.
     */
    static caml::CAMLMap load_map_(const std::string& path) {
        std::filesystem::path p(path);
        std::string ext = p.extension().string();
        // Lowercase extension comparison.
        for (char& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".caml") {
            return caml::CAMLMap::load_caml(path);
        }
        // Default: treat as YAML (.yaml, .yml, or any other extension).
        return caml::CAMLMap::load_yaml(path);
    }

    /**
     * @brief Recursively parses a SceneObject from a YAML node.
     */
    static std::unique_ptr<SceneObject> parse_object_(
        const fkyaml::node& node,
        TransformComponent* parent_transform,
        const std::string&  scene_dir,
        coopa::gfx::core::Device& device,
        coopa::gfx::memory::Allocator& allocator,
        coopa::gfx::command::CommandPool& cmd_pool,
        std::unordered_map<std::string, std::shared_ptr<coopa::gfx::engine::data::Mesh>>& mesh_cache)
    {
        // Name and active flag.
        std::string name   = node.contains("name")   ? node.at("name").get_value<std::string>() : "Object";
        bool        active = node.contains("active")  ? node.at("active").get_value<bool>()      : true;

        auto obj = std::make_unique<SceneObject>(name, active);

        // Always add a TransformComponent first.
        auto* tc = obj->add_component<TransformComponent>();

        // Link to parent transform for hierarchy.
        if (parent_transform) {
            tc->set_parent_transform(&parent_transform->transform());
        }

        // Parse components[].
        if (node.contains("components")) {
            for (const auto& comp_node : node.at("components")) {
                parse_component_(comp_node, *obj, *tc,
                                 scene_dir, device, allocator, cmd_pool, mesh_cache);
            }
        }

        // Parse children[] recursively.
        if (node.contains("children")) {
            for (const auto& child_node : node.at("children")) {
                auto child = parse_object_(child_node, tc,
                                           scene_dir, device, allocator, cmd_pool, mesh_cache);
                obj->add_child(std::move(child));
            }
        }

        return obj;
    }

    /**
     * @brief Parses a single component node and attaches it to the SceneObject.
     *
     * fkYAML exposes YAML tags (e.g. "!Transform") via node.tag_name().
     */
    static void parse_component_(
        const fkyaml::node& node,
        SceneObject&        obj,
        TransformComponent& tc,
        const std::string&  scene_dir,
        coopa::gfx::core::Device& device,
        coopa::gfx::memory::Allocator& allocator,
        coopa::gfx::command::CommandPool& cmd_pool,
        std::unordered_map<std::string, std::shared_ptr<coopa::gfx::engine::data::Mesh>>& mesh_cache)
    {
        // Retrieve the YAML tag to identify the component type.
        std::string tag;
        if (!node.has_tag_name()) {
            // No tag — check for a "type" key as fallback.
            if (!node.contains("type")) return;
            tag = node.at("type").get_value<std::string>();
        } else {
            tag = node.get_tag_name(); // e.g. "!Transform", "!MeshRenderer", "!Camera"
        }

        if (tag == "!Transform" || tag == "Transform") {
            // Apply transform data to the already-created TransformComponent.
            tc.transform().set_from_node(node);

        } else if (tag == "!MeshRenderer" || tag == "MeshRenderer") {
            auto* mr = obj.add_component<MeshRenderer>();
            std::string mesh_path_key;
            if (node.contains("mesh_path")) {
                mesh_path_key = node.at("mesh_path").get_value<std::string>();
            }
            mr->set_mesh_path(mesh_path_key);

            if (node.contains("affects_reflection_probes"))
                mr->affects_reflection_probes = node.at("affects_reflection_probes").get_value<bool>();

            if (!mesh_path_key.empty()) {
                // Load and cache the GPU mesh.
                if (mesh_cache.count(mesh_path_key) == 0) {
                    std::string mesh_file = scene_dir + "/meshes/" + mesh_path_key + ".yaml";
                    try {
                        caml::CAMLMap mesh_map = caml::CAMLMap::load_yaml(mesh_file);
                        auto gpu_mesh = std::make_shared<coopa::gfx::engine::data::Mesh>(
                            coopa::gfx::engine::data::Mesh::from_node(
                                device, allocator, cmd_pool, mesh_map.get_raw_node()
                            )
                        );
                        mesh_cache[mesh_path_key] = gpu_mesh;
                    } catch (const std::exception& e) {
                        std::cerr << "[SceneLoader] Failed to load mesh '" << mesh_file
                                  << "': " << e.what() << std::endl;
                    }
                }
                if (mesh_cache.count(mesh_path_key)) {
                    mr->set_mesh(mesh_cache.at(mesh_path_key));
                }
            }

            if (node.contains("material")) {
                const auto& mat_node = node.at("material");
                if (mat_node.contains("albedo")) {
                    const auto& alb = mat_node.at("albedo");
                    mr->material.albedo = {
                        alb.at("r").get_value<float>(),
                        alb.at("g").get_value<float>(),
                        alb.at("b").get_value<float>()
                    };
                }
                if (mat_node.contains("metallic"))  mr->material.metallic  = mat_node.at("metallic").get_value<float>();
                if (mat_node.contains("roughness")) mr->material.roughness = mat_node.at("roughness").get_value<float>();
                if (mat_node.contains("ao"))        mr->material.ao        = mat_node.at("ao").get_value<float>();

                if (mat_node.contains("alpha"))        mr->material.alpha        = mat_node.at("alpha").get_value<float>();
                if (mat_node.contains("alpha_cutoff")) mr->material.alpha_cutoff = mat_node.at("alpha_cutoff").get_value<float>();
                if (mat_node.contains("alpha_mode")) {
                    std::string mode = mat_node.at("alpha_mode").get_value<std::string>();
                    if (mode == "BLEND") {
                        mr->material.alpha_mode = AlphaMode::Blend;
                    } else if (mode == "MASK" || mode == "CLIP") {
                        mr->material.alpha_mode = AlphaMode::Mask;
                    } else {
                        mr->material.alpha_mode = AlphaMode::Opaque;
                    }
                }

                if (mat_node.contains("texture_albedo"))
                    mr->material.texture_albedo = mat_node.at("texture_albedo").get_value<std::string>();
                if (mat_node.contains("texture_normal"))
                    mr->material.texture_normal = mat_node.at("texture_normal").get_value<std::string>();
                if (mat_node.contains("texture_metallic_roughness"))
                    mr->material.texture_metallic_roughness = mat_node.at("texture_metallic_roughness").get_value<std::string>();
            }

        } else if (tag == "!Camera" || tag == "Camera") {
            auto* cam = obj.add_component<CameraComponent>();

            // Projection mode: type/projection key
            if (node.contains("projection")) {
                std::string p_str = node.at("projection").get_value<std::string>();
                cam->type = (p_str == "ORTHO" || p_str == "Orthographic" || p_str == "orthographic")
                            ? CameraType::Orthographic : CameraType::Perspective;
            } else if (node.contains("type")) {
                std::string type_str = node.at("type").get_value<std::string>();
                if (type_str != "Camera") {
                    cam->type = (type_str == "ORTHO" || type_str == "Orthographic" || type_str == "orthographic")
                                ? CameraType::Orthographic : CameraType::Perspective;
                }
            }

            if (node.contains("fov")) cam->fov = node.at("fov").get_value<float>();

            if (node.contains("orthographic_size")) {
                cam->orthographic_size = node.at("orthographic_size").get_value<float>();
            } else if (node.contains("ortho_size")) {
                cam->orthographic_size = node.at("ortho_size").get_value<float>();
            } else if (node.contains("ortho_scale")) {
                cam->set_ortho_scale(node.at("ortho_scale").get_value<float>());
            }

            if (node.contains("near_clip_plane")) {
                cam->clip_start = node.at("near_clip_plane").get_value<float>();
            } else if (node.contains("clip_start")) {
                cam->clip_start = node.at("clip_start").get_value<float>();
            }

            if (node.contains("far_clip_plane")) {
                cam->clip_end = node.at("far_clip_plane").get_value<float>();
            } else if (node.contains("clip_end")) {
                cam->clip_end = node.at("clip_end").get_value<float>();
            }

            if (node.contains("lens"))          cam->lens          = node.at("lens").get_value<float>();
            if (node.contains("sensor_width"))  cam->sensor_width  = node.at("sensor_width").get_value<float>();
            if (node.contains("sensor_height")) cam->sensor_height = node.at("sensor_height").get_value<float>();

        } else if (tag == "!DirectionalLight" || tag == "DirectionalLight") {
            auto* dl = obj.add_component<DirectionalLightComponent>();

            // direction: { x, y, z }
            if (node.contains("direction")) {
                const auto& d = node.at("direction");
                float dx = d.contains("x") ? d.at("x").get_value<float>() : dl->direction.x;
                float dy = d.contains("y") ? d.at("y").get_value<float>() : dl->direction.y;
                float dz = d.contains("z") ? d.at("z").get_value<float>() : dl->direction.z;
                dl->direction = glm::normalize(glm::vec3(dx, dy, dz));
            }
            // color: { r, g, b }
            if (node.contains("color")) {
                const auto& c = node.at("color");
                dl->color.r = c.contains("r") ? c.at("r").get_value<float>() : dl->color.r;
                dl->color.g = c.contains("g") ? c.at("g").get_value<float>() : dl->color.g;
                dl->color.b = c.contains("b") ? c.at("b").get_value<float>() : dl->color.b;
            }
            if (node.contains("intensity")) {
                dl->intensity = node.at("intensity").get_value<float>();
            }
            // ambient: { r, g, b }
            if (node.contains("ambient")) {
                const auto& a = node.at("ambient");
                dl->ambient.r = a.contains("r") ? a.at("r").get_value<float>() : dl->ambient.r;
                dl->ambient.g = a.contains("g") ? a.at("g").get_value<float>() : dl->ambient.g;
                dl->ambient.b = a.contains("b") ? a.at("b").get_value<float>() : dl->ambient.b;
            }
            if (node.contains("cast_shadows")) {
                dl->cast_shadows = node.at("cast_shadows").get_value<bool>();
            }

        } else if (tag == "!PointLight" || tag == "PointLight") {
            auto* pl = obj.add_component<PointLightComponent>();

            // color: { r, g, b }
            if (node.contains("color")) {
                const auto& c = node.at("color");
                pl->color.r = c.contains("r") ? c.at("r").get_value<float>() : pl->color.r;
                pl->color.g = c.contains("g") ? c.at("g").get_value<float>() : pl->color.g;
                pl->color.b = c.contains("b") ? c.at("b").get_value<float>() : pl->color.b;
            }
            if (node.contains("intensity")) {
                pl->intensity = node.at("intensity").get_value<float>();
            }
            if (node.contains("range")) {
                pl->range = node.at("range").get_value<float>();
            }
            if (node.contains("cast_shadows")) {
                pl->cast_shadows = node.at("cast_shadows").get_value<bool>();
            }
            if (node.contains("attenuation_constant")) {
                pl->attenuation_constant = node.at("attenuation_constant").get_value<float>();
            }
            if (node.contains("attenuation_linear")) {
                pl->attenuation_linear = node.at("attenuation_linear").get_value<float>();
            }
            if (node.contains("attenuation_quadratic")) {
                pl->attenuation_quadratic = node.at("attenuation_quadratic").get_value<float>();
            }

        } else if (tag == "!GiProbeVolume" || tag == "GiProbeVolume") {
            auto* gv = obj.add_component<GiProbeVolumeComponent>();
            if (node.contains("origin")) {
                const auto& o = node.at("origin");
                gv->origin.x = o.contains("x") ? o.at("x").get_value<float>() : gv->origin.x;
                gv->origin.y = o.contains("y") ? o.at("y").get_value<float>() : gv->origin.y;
                gv->origin.z = o.contains("z") ? o.at("z").get_value<float>() : gv->origin.z;
            }
            if (node.contains("extent")) {
                const auto& e = node.at("extent");
                gv->extent.x = e.contains("x") ? e.at("x").get_value<float>() : gv->extent.x;
                gv->extent.y = e.contains("y") ? e.at("y").get_value<float>() : gv->extent.y;
                gv->extent.z = e.contains("z") ? e.at("z").get_value<float>() : gv->extent.z;
            }
            if (node.contains("grid_resolution")) {
                const auto& g = node.at("grid_resolution");
                gv->grid_resolution.x = g.contains("x") ? g.at("x").get_value<int>() : gv->grid_resolution.x;
                gv->grid_resolution.y = g.contains("y") ? g.at("y").get_value<int>() : gv->grid_resolution.y;
                gv->grid_resolution.z = g.contains("z") ? g.at("z").get_value<int>() : gv->grid_resolution.z;
            }
            if (node.contains("update_mode")) {
                std::string mode = node.at("update_mode").get_value<std::string>();
                gv->update_mode = (mode == "dynamic") ? GiUpdateMode::Dynamic : GiUpdateMode::Static;
            }
            if (node.contains("gi_intensity"))
                gv->gi_intensity = node.at("gi_intensity").get_value<float>();

        } else if (tag == "!ReflectionProbe" || tag == "ReflectionProbe") {
            auto* rp = obj.add_component<ReflectionProbeComponent>();
            if (node.contains("box_extent")) {
                const auto& e = node.at("box_extent");
                rp->box_extent.x = e.contains("x") ? e.at("x").get_value<float>() : rp->box_extent.x;
                rp->box_extent.y = e.contains("y") ? e.at("y").get_value<float>() : rp->box_extent.y;
                rp->box_extent.z = e.contains("z") ? e.at("z").get_value<float>() : rp->box_extent.z;
            }
            if (node.contains("resolution"))
                rp->resolution = static_cast<uint32_t>(node.at("resolution").get_value<int>());
            if (node.contains("blend_distance"))
                rp->blend_distance = node.at("blend_distance").get_value<float>();
            if (node.contains("importance"))
                rp->importance = node.at("importance").get_value<int>();
            if (node.contains("intensity"))
                rp->intensity = node.at("intensity").get_value<float>();

        } else if (tag == "!EnvironmentLight" || tag == "EnvironmentLight") {
            auto* el = obj.add_component<EnvironmentLightComponent>();
            if (node.contains("sky_color")) {
                const auto& c = node.at("sky_color");
                el->sky_color.r = c.contains("r") ? c.at("r").get_value<float>() : el->sky_color.r;
                el->sky_color.g = c.contains("g") ? c.at("g").get_value<float>() : el->sky_color.g;
                el->sky_color.b = c.contains("b") ? c.at("b").get_value<float>() : el->sky_color.b;
            }
            if (node.contains("ground_color")) {
                const auto& c = node.at("ground_color");
                el->ground_color.r = c.contains("r") ? c.at("r").get_value<float>() : el->ground_color.r;
                el->ground_color.g = c.contains("g") ? c.at("g").get_value<float>() : el->ground_color.g;
                el->ground_color.b = c.contains("b") ? c.at("b").get_value<float>() : el->ground_color.b;
            }
            if (node.contains("sky_intensity"))
                el->sky_intensity = node.at("sky_intensity").get_value<float>();
            if (node.contains("hdri_path"))
                el->hdri_path = node.at("hdri_path").get_value<std::string>();
        } else if (tag == "!Animation" || tag == "Animation" || tag == "!AnimationComponent" || tag == "AnimationComponent") {
            auto* anim = obj.add_component<AnimationComponent>();
            std::string anim_file;
            if (node.contains("animation_file")) {
                anim_file = node.at("animation_file").get_value<std::string>();
            } else if (node.contains("file")) {
                anim_file = node.at("file").get_value<std::string>();
            }

            if (!anim_file.empty()) {
                std::filesystem::path p(anim_file);
                if (p.is_relative()) {
                    std::string scene_relative = scene_dir + "/" + anim_file;
                    if (std::filesystem::exists(scene_relative)) {
                        anim->load_from_yaml(scene_relative);
                    } else if (std::filesystem::exists(anim_file)) {
                        anim->load_from_yaml(anim_file);
                    } else {
                        anim->load_from_yaml(anim_file);
                    }
                } else {
                    anim->load_from_yaml(anim_file);
                }
            }
            anim->parse_node(node);
        } else {
            // Not a built-in tag — consult the externally-registered registry.
            // Unrecognized tags still fall through silently for forward compatibility.
            auto& registry = parsers_();
            auto it = registry.find(tag);
            if (it != registry.end()) {
                it->second(node, obj);
            }
        }
    }

    /**
     * @brief Depth-first search for a DirectionalLightComponent in the hierarchy.
     * @return Non-owning pointer to the first found component, or nullptr.
     */
    static DirectionalLightComponent* find_light_(const SceneObject& obj) {
        DirectionalLightComponent* light = nullptr;
        obj.for_each_recursive([&](const SceneObject& o) {
            if (!light && o.active()) {
                light = const_cast<SceneObject&>(o).get_component<DirectionalLightComponent>();
            }
        });
        return light;
    }

    /**
     * @brief Depth-first search for a CameraComponent in the object hierarchy.
     * @return Non-owning pointer to the first found CameraComponent, or nullptr.
     */
    static CameraComponent* find_camera_(const SceneObject& obj) {
        CameraComponent* cam = nullptr;
        obj.for_each_recursive([&](const SceneObject& o) {
            if (!cam) {
                cam = const_cast<SceneObject&>(o).get_component<CameraComponent>();
            }
        });
        return cam;
    }
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_LOADER_H
