/**
 * @file scene_loader.h
 * @brief Loads a Scene from a YAML document using fkYAML.
 *
 * SceneLoader itself understands only hierarchy and Transform/Animation —
 * the two component types libcoopa defines. Every other component (mesh
 * renderers, cameras, lights, UI widgets, ...) is parsed by callbacks
 * registered from outside libcoopa via register_component_parser(), so the
 * scene system never depends on gfxcoopa, uicoopa, or any other consumer.
 *
 * Scene format:
 *   format: <free-form string, informational only>
 *   scene:
 *     inherit_from: <path.yaml>              # optional; whole-file inheritance, see below
 *     scene_name: <name>
 *     auto_transform: <bool, default true>   # add a TransformComponent to every object
 *     root_objects:
 *       - name: <str>
 *         active: <bool>
 *         inherit_from: <path.yaml[#Object]> # optional; this object's base, merged in first
 *         components:
 *           - !Transform  { position, rotation, scale }   # or "type: Transform"
 *           - !SomeTag    { ... }                          # dispatched via the registry
 *         children: [...]
 *
 * Both a leading-bang YAML tag ("!Foo") and a "type: Foo" key are accepted
 * and normalized to the same bare name before dispatch, so a single parser
 * registration matches either spelling.
 *
 * Known fkYAML limitation (vendored version): a block-style "!Tag" mapping
 * (tag on its own line, keys indented below it) parses fine as the FIRST item
 * of a sequence, but fails with "Detected invalid indentation" as the second
 * or later item — e.g. `- !RectTransform\n  x: 1\n- !Image\n  y: 2` throws on
 * the second entry. Flow-style ("!Tag { x: 1 }") and the "type: Foo" key form
 * are both unaffected. Prefer "type:" for any components[] list with more than
 * one entry until this is fixed upstream; every scene file in this codebase
 * uses that form for exactly this reason.
 *
 * By default, YAML is parsed directly via fkYAML::node::deserialize(). Set a
 * document loader (set_document_loader()) to route through something else —
 * e.g. an application that wants encrypted/compressed .caml scene files can
 * plug in a caml-backed loader without SceneLoader depending on caml itself.
 *
 * Before any of the above is parsed, every `inherit_from` reachable from the
 * document is expanded by SceneInheritance (see scene_inherit.h) into one
 * fully-merged node — this happens unconditionally, even for documents routed
 * through a custom document loader, so `inherit_from` works the same way
 * regardless of where the raw YAML came from. See scene_inherit.h for the
 * object-level/scene-level inheritance and merge rules; ParseContext::resolve()
 * below is the seam that keeps a merged-in file's relative asset paths
 * resolving against the directory that actually declared them.
 */

#ifndef COOPA_SCENE_SCENE_LOADER_H
#define COOPA_SCENE_SCENE_LOADER_H

#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene_inherit.h>
#include <coopa/scene/components/transform_component.h>
#include <coopa/scene/components/animation_component.h>

#include <fkYAML/node.hpp>

#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <functional>
#include <vector>

namespace coopa {
namespace scene {

/**
 * @class SceneLoader
 * @brief Parses a YAML scene file into a Scene, dispatching component parsing
 *        to registered callbacks.
 *
 * Usage:
 * @code
 * // Once at startup, from whichever module defines each component:
 * SceneLoader::register_component_parser("MeshRenderer", [&](const auto& node, auto& obj, const auto& ctx) {
 *     auto* mr = obj.template add_component<MeshRenderer>();
 *     // ... populate mr from node, resolving relative asset paths via ctx.resolve(path) ...
 * });
 *
 * Scene scene = SceneLoader::load("assets/scenes/cube/scene.yaml");
 * @endcode
 */
class SceneLoader {
public:
    /**
     * @brief Context passed to every component parser alongside its YAML node.
     */
    struct ParseContext {
        std::string scene_path; /**< Full path of the root scene file being loaded. */
        std::string scene_dir;  /**< Its directory; resolve relative asset paths against this. */

        /**
         * @brief Directories to try first when resolving a relative asset
         *        path, nearest declaring file first.
         *
         * Populated per-component from that component's SceneInheritance
         * `__source_dirs` provenance, so a component merged in from a prefab
         * in a different directory (e.g. assets/prefabs/) still resolves its
         * own relative paths (e.g. `font: ../fonts/x.ttf`) against the file
         * that actually wrote them, not against the including scene's
         * directory. Empty when a node carries no provenance (e.g. an
         * object built without ever going through SceneLoader::load()).
         */
        std::vector<std::string> search_dirs;

        /**
         * @brief Resolves a relative asset path against this context's provenance.
         *
         * Tries search_dirs in order, then scene_dir, returning the first
         * candidate that exists on disk. Falls through to the path
         * unchanged if none exist, so callers with their own asset search
         * roots (e.g. coopa::asset::AssetSource) can still make sense of it.
         *
         * @param relative Path as written in the scene file.
         * @return The resolved path, or `relative` unchanged if no candidate exists.
         */
        std::string resolve(const std::string& relative) const {
            if (std::filesystem::path(relative).is_absolute()) return relative;
            for (const auto& dir : search_dirs) {
                std::filesystem::path candidate = std::filesystem::path(dir) / relative;
                if (std::filesystem::exists(candidate)) return candidate.string();
            }
            std::filesystem::path candidate = std::filesystem::path(scene_dir) / relative;
            if (std::filesystem::exists(candidate)) return candidate.string();
            return relative;
        }
    };

    /**
     * @brief Signature for a registered component parser.
     *
     * Receives the raw component YAML node, the SceneObject it should attach
     * to (already carrying its TransformComponent if auto_transform is on),
     * and the ParseContext for resolving relative asset paths. Implementations
     * should call `obj.add_component<T>()` and populate it from `node`.
     */
    using ComponentParser = std::function<void(const fkyaml::node& node, SceneObject& obj, const ParseContext& ctx)>;

    /**
     * @brief Signature for a pluggable raw-document loader.
     *
     * Receives a scene file path and must return its parsed root fkyaml::node.
     * Lets an application route scene loading through something other than a
     * plain YAML file on disk (e.g. an encrypted/compressed container) without
     * SceneLoader depending on whatever library that requires.
     */
    using DocumentLoader = std::function<fkyaml::node(const std::string& path)>;

    /**
     * @brief Loads a scene from a YAML file (or whatever set_document_loader() routes to).
     *
     * Constructs the full Scene hierarchy. Every non-Transform/Animation
     * component is handed to whatever parser is registered for its tag; an
     * unregistered tag is silently skipped for forward compatibility.
     *
     * @param path Absolute or relative path to the scene file.
     * @return Fully constructed Scene.
     * @throws std::runtime_error on parse errors or missing files.
     */
    static Scene load(const std::string& path) {
        std::filesystem::path scene_path(path);
        std::filesystem::path scene_dir = scene_path.parent_path();
        ParseContext ctx{ path, scene_dir.string(), { scene_dir.string() } };

        fkyaml::node root = SceneInheritance::resolve(
            path, [](const std::string& p) { return load_node_(p); });

        std::string scene_name = "Scene";
        bool auto_transform = true;
        if (root.contains("scene")) {
            const auto& scene_node = root.at("scene");
            if (scene_node.contains("scene_name")) {
                scene_name = scene_node.at("scene_name").get_value<std::string>();
            }
            if (scene_node.contains("auto_transform")) {
                auto_transform = scene_node.at("auto_transform").get_value<bool>();
            }
        }

        Scene scene(scene_name);

        if (!root.contains("scene") || !root.at("scene").contains("root_objects")) {
            return scene; // Empty scene is valid.
        }

        for (const auto& obj_node : root.at("scene").at("root_objects")) {
            auto obj = parse_object_(obj_node, nullptr, auto_transform, ctx);
            scene.add_root_object(std::move(obj));
        }

        scene.start();
        return scene;
    }

    /**
     * @brief Registers a parser for a component's YAML tag/type name.
     *
     * Lets libraries outside libcoopa (e.g. gfxcoopa, uicoopa) extend scene
     * loading without SceneLoader depending on them. Registering the same
     * name twice replaces the previous parser. Names are matched after
     * normalization: a leading '!' is stripped, so registering "MeshRenderer"
     * matches both a "!MeshRenderer" YAML tag and a "type: MeshRenderer" key.
     * Built-in names ("Transform", "Animation") cannot be overridden — they
     * are handled before the registry is consulted.
     *
     * @param name Component name as it appears in the scene file, with or without a leading '!'.
     * @param fn   Parser invoked when a component node resolves to this name.
     */
    static void register_component_parser(const std::string& name, ComponentParser fn) {
        parsers_()[normalize_tag_(name)] = std::move(fn);
    }

    /**
     * @brief Clears every registered component parser.
     *
     * Registered parsers commonly capture references (e.g. a Device or
     * Allocator) by reference in their closures; call this before those
     * referenced objects are destroyed to avoid dangling captures surviving
     * into a later SceneLoader::load() call.
     */
    static void clear_component_parsers() {
        parsers_().clear();
    }

    /**
     * @brief Installs a custom raw-document loader, consulted by every subsequent load().
     *
     * Pass an empty std::function to restore the default fkYAML-file behavior.
     *
     * @param fn Loader callback, or an empty function to reset to the default.
     */
    static void set_document_loader(DocumentLoader fn) {
        document_loader_() = std::move(fn);
    }

private:
    /** @brief Function-local static registry of registered component parsers. */
    static std::unordered_map<std::string, ComponentParser>& parsers_() {
        static std::unordered_map<std::string, ComponentParser> registry;
        return registry;
    }

    /** @brief Function-local static holder for the optional custom document loader. */
    static DocumentLoader& document_loader_() {
        static DocumentLoader loader;
        return loader;
    }

    /** @brief Strips a leading '!' so tag-form and type-form names compare equal. */
    static std::string normalize_tag_(const std::string& tag) {
        return (!tag.empty() && tag[0] == '!') ? tag.substr(1) : tag;
    }

    /** @brief Loads and parses the root node, via the custom loader if one is installed. */
    static fkyaml::node load_node_(const std::string& path) {
        if (document_loader_()) {
            return document_loader_()(path);
        }
        std::ifstream ifs(path);
        if (!ifs) {
            throw std::runtime_error("[SceneLoader] Failed to open scene file: " + path);
        }
        return fkyaml::node::deserialize(ifs);
    }

    /**
     * @brief Recursively parses a SceneObject from a YAML node.
     */
    static std::unique_ptr<SceneObject> parse_object_(
        const fkyaml::node& node,
        TransformComponent* parent_transform,
        bool auto_transform,
        const ParseContext& ctx)
    {
        std::string name   = node.contains("name")   ? node.at("name").get_value<std::string>() : "Object";
        bool        active = node.contains("active")  ? node.at("active").get_value<bool>()      : true;

        auto obj = std::make_unique<SceneObject>(name, active);

        TransformComponent* tc = nullptr;
        if (auto_transform) {
            tc = obj->add_component<TransformComponent>();
            if (parent_transform) {
                tc->set_parent_transform(&parent_transform->transform());
            }
        }

        if (node.contains("components")) {
            for (const auto& comp_node : node.at("components")) {
                ParseContext comp_ctx = ctx;
                if (comp_node.contains(SceneInheritance::kSourceDirsKey)) {
                    comp_ctx.search_dirs.clear();
                    for (const auto& d : comp_node.at(SceneInheritance::kSourceDirsKey)) {
                        comp_ctx.search_dirs.push_back(d.get_value<std::string>());
                    }
                }
                parse_component_(comp_node, *obj, comp_ctx);
            }
        }

        if (node.contains("children")) {
            for (const auto& child_node : node.at("children")) {
                auto child = parse_object_(child_node, tc, auto_transform, ctx);
                obj->add_child(std::move(child));
            }
        }

        return obj;
    }

    /**
     * @brief Parses a single component node and attaches it to the SceneObject.
     *
     * fkYAML exposes YAML tags (e.g. "!Transform") via node.get_tag_name();
     * a "type:" key is the fallback for untagged nodes.
     */
    static void parse_component_(const fkyaml::node& node, SceneObject& obj, const ParseContext& ctx) {
        std::string raw_tag;
        if (!node.has_tag_name()) {
            if (!node.contains("type")) return;
            raw_tag = node.at("type").get_value<std::string>();
        } else {
            raw_tag = node.get_tag_name();
        }
        std::string tag = normalize_tag_(raw_tag);

        if (tag == "Transform") {
            if (auto* tc = obj.get_transform()) {
                tc->transform().set_from_node(node);
            }
            // No TransformComponent to apply to (auto_transform: false) — silently skipped;
            // a scene that opts out of auto transforms should not carry !Transform nodes.
        } else if (tag == "Animation" || tag == "AnimationComponent") {
            auto* anim = obj.add_component<AnimationComponent>();
            std::string anim_file;
            if (node.contains("animation_file")) {
                anim_file = node.at("animation_file").get_value<std::string>();
            } else if (node.contains("file")) {
                anim_file = node.at("file").get_value<std::string>();
            }

            if (!anim_file.empty()) {
                anim->load_from_yaml(ctx.resolve(anim_file));
            }
            anim->parse_node(node);
        } else {
            auto& registry = parsers_();
            auto it = registry.find(tag);
            if (it != registry.end()) {
                it->second(node, obj, ctx);
            }
            // Unrecognized names fall through silently for forward compatibility.
        }
    }
};

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_LOADER_H
