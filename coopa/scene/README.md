# Scene Module (`coopa::scene`)

The `scene` module provides the scene graph hierarchy, component-based entity
model, scene lifecycle management, and YAML scene loading. It is
**dependency-free**: nothing in this module includes gfxcoopa, caml, uicoopa,
or any other sibling repo — only libcoopa's own vendored `fkYAML` and `glm`.
Every renderer-specific or UI-specific component (mesh renderers, cameras,
lights, GI/reflection probes, RectTransform, ...) lives outside libcoopa and
is parsed via `SceneLoader::register_component_parser()`, never by this
module directly. gfxcoopa's `engine::components::register_render_components()`
and uicoopa's `register_ui_components()` are the two current registrants.

---

## Class Hierarchy & Ownership Model

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                              SCENE GRAPH CLASS HIERARCHY                                 │
└──────────────────────────────────────────────────────────────────────────────────────────┘

                                ┌───────────────────────┐
                                │     SceneManager      │
                                ├───────────────────────┤
                                │ unique_ptr<Scene>     │
                                └───────────┬───────────┘
                                            │ owns active_scene_
                                            ▼
                                ┌───────────────────────┐
                                │        Scene          │
                                ├───────────────────────┤
                                │ vector<SceneObject*>  │
                                └───────────┬───────────┘
                                            │ owns root_objects_
                                            ▼
                                ┌───────────────────────┐
                                │      SceneObject      │ ◄───┐
                                ├───────────────────────┤     │ owns
                                │ vector<Component*>    │ ────┘ children_
                                │ vector<SceneObject*>  │
                                └───────────┬───────────┘
                                            │ owns components_
                                            ▼
                                ┌───────────────────────┐
                                │       Component       │
                                ├───────────────────────┤
                                │ Transform, Animation  │  (defined here)
                                │ MeshRenderer, Camera,  │  (defined by gfxcoopa)
                                │ RectTransform, Canvas  │  (defined by uicoopa)
                                └───────────────────────┘
```

---

## Scene Loading Sequence

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                                 SCENE LOADING LIFECYCLE                                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘

  Application            SceneManager             SceneLoader              fkYAML
      │                       │                        │                     │
      │── register_component_parser(...) for every non-Transform/Animation component
      │                       │                        │                     │
      │── load_scene(path) ──►│                        │                     │
      │                       │── load(path) ─────────►│                     │
      │                       │                        │── deserialize() ───►│
      │                       │                        │◄─ fkyaml::node ─────│
      │                       │                        │
      │                       │                        │── Build Scene Tree
      │                       │                        │── Dispatch each component
      │                       │                        │   node to its registered
      │                       │                        │   parser by tag/type name
      │                       │◄── Scene Instance ─────│
      │◄── Scene Ready ───────│
```

---

## Structural Specifications & Ownership Matrix

| Class | Primary Owner | Contained Members | Responsibility |
|---|---|---|---|
| `SceneManager` | Main Engine Application | `std::unique_ptr<Scene>` | High-level orchestrator managing active scene swapping and lifecycle |
| `Scene` | `SceneManager` | `std::vector<std::unique_ptr<SceneObject>>` | Root scene node holding top-level objects; generic component queries |
| `SceneObject` | `Scene` or parent `SceneObject` | `std::vector<std::unique_ptr<Component>>`, `children` | Entity node holding component instances and recursive child hierarchy |
| `Component` | `SceneObject` | Non-owning raw `owner` pointer | Abstract base class for all component behaviors |
| `SceneLoader` | Static/stateless | Registry of external `ComponentParser`s | Parses `.yaml` scene descriptions, dispatching non-built-in components externally |

---

## File Breakdown

### [`component.h`](file:///home/coopa/git/libcoopa/coopa/scene/component.h)

Abstract base class for all scene components:
- `owner`: Non-owning raw pointer to the host `SceneObject`.
- `virtual void start()`: Invoked once after scene instantiation.
- `virtual void update(float delta_time)`: Invoked every frame during scene update.
- `virtual std::string type_name() const = 0`: Returns human-readable component type name.

### [`scene.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene.h)

Root container for a loaded scene. Owns all root-level `SceneObject` instances
via `std::unique_ptr`, and knows nothing about any specific component type —
callers use these generic templated queries instead:
- `get_components<T>()`: Flat vector of every active `T` in the hierarchy.
- `find_objects_with<T>()`: Flat vector of every active `SceneObject` carrying a `T`.
- `find_first_component<T>()`: The first active `T` found, pre-order.
- `find_object(name)`: Depth-first search for an object by name.

Renderer-facing code (blendy, gfxcoopa's GI system) builds these into a more
convenient shape via gfxcoopa's `engine::components::SceneView` adapter rather
than calling them directly.

### [`scene_object.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_object.h)

The core node in the scene tree (equivalent to Unity's `GameObject`):
- Holds object name, active state flag, non-owning parent pointer, list of owned components, and list of owned child objects.
- Component API: `add_component<T>(...)`, `get_component<T>()`, `attach_component(...)`.
- Convenient getter: `get_transform()` (the one component type this module defines).
- Hierarchy API: `add_child(...)`, `detach_child(...)`, `set_parent(...)`, `find_descendant(name)`, `for_each_recursive(fn)`.
- Life-cycle methods (`start()`, `update(dt)`) recurse through attached components and active children.

### [`scene_manager.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_manager.h)

High-level manager for scene lifecycle:
- Wraps scene loading (`load_scene`) via `SceneLoader`. Default-constructible —
  carries no GPU handles of its own.
- Owns the currently active `Scene`.
- Delegates frame updates (`update(delta_time)`) to the active scene.

### [`scene_loader.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_loader.h)

Parser for YAML scene files:
- Understands hierarchy plus the two component types this module defines:
  `!Transform` / `type: Transform` and `!Animation` / `type: Animation`.
- Every other component name is dispatched to whatever parser was registered
  for it via `register_component_parser(name, fn)`; unregistered names are
  silently skipped. A leading `!` is stripped before matching, so a YAML tag
  and a `type:` key spelling reach the same registered parser.
- `set_document_loader(fn)` lets an application route parsing through
  something other than a plain file on disk — e.g. an encrypted/compressed
  container — without this module depending on whatever that requires.
- `ParseContext{scene_path, scene_dir}` is passed to every parser so it can
  resolve asset paths relative to the scene file.

---

## Component Submodule

For detailed documentation on the two component types defined here, see the
[Components Submodule README](file:///home/coopa/git/libcoopa/coopa/scene/components/README.md).
Renderer components live in gfxcoopa's `engine/components/`; UI components
live in uicoopa's `uicoopa/`.

---

## Usage Example

```cpp
#include <coopa/scene/scene_manager.h>
#include <coopa/scene/scene_object.h>
#include <gfxcoopa/engine/components/register.h>

// 1. Register every non-Transform/Animation component this application needs.
coopa::gfx::engine::components::register_render_components(device, allocator, cmd_pool);

// 2. Initialize scene manager and load a scene.
coopa::scene::SceneManager manager;
manager.load_scene("assets/scenes/gi_cornell_box/scene.yaml");

// 3. Obtain active scene.
auto& scene = manager.get_active_scene();

// 4. Update scene hierarchy per frame.
float delta_time = 0.016f; // 60 FPS frame delta
manager.update(delta_time);
```
