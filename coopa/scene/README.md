# Scene Module (`coopa::scene`)

The `scene` module provides the scene graph hierarchy, component-based entity model, scene lifecycle management, and YAML/CAML scene loading for **Blendy**.

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
                                │ Transform, Mesh,      │
                                │ Camera, Lights...     │
                                └───────────────────────┘
```

---

## Scene Loading Sequence

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                                 SCENE LOADING LIFECYCLE                                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘

  Application           SceneManager            SceneLoader           caml::CAMLMap
      │                      │                       │                      │
      │── load_scene(path) ─►│                       │                      │
      │                      │── load(path, ...) ───►│                      │
      │                      │                       │── load_map_(path) ──►│
      │                      │                       │◄─ fkyaml::node ──────│
      │                      │                       │
      │                      │                       │── Build Scene Tree
      │                      │                       │── Upload GPU Meshes
      │                      │◄── Scene Instance ────│
      │◄── Scene Ready ──────│
```

---

## Structural Specifications & Ownership Matrix

| Class | Primary Owner | Contained Members | Responsibility |
|---|---|---|---|
| `SceneManager` | Main Engine Application | `std::unique_ptr<Scene>` | High-level orchestrator managing active scene swapping and lifecycle |
| `Scene` | `SceneManager` | `std::vector<std::unique_ptr<SceneObject>>` | Root scene node holding top-level objects, active camera, and directional light pointers |
| `SceneObject` | `Scene` or parent `SceneObject` | `std::vector<std::unique_ptr<Component>>`, `children` | Entity node holding component instances and recursive child hierarchy |
| `Component` | `SceneObject` | Non-owning raw `owner` pointer | Abstract base class for transform, visual, lighting, probe, and animation behaviors |
| `SceneLoader` | `SceneManager` (transient context) | Asset cache maps (`Mesh`, GPU resources) | Parses `.yaml`/`.caml` scene descriptions and uploads GPU assets |

---

## File Breakdown

### [`component.h`](file:///home/coopa/git/libcoopa/coopa/scene/component.h)

Abstract base class for all scene components:
- `owner`: Non-owning raw pointer to the host `SceneObject`.
- `virtual void start()`: Invoked once after scene instantiation.
- `virtual void update(float delta_time)`: Invoked every frame during scene update.
- `virtual std::string type_name() const = 0`: Returns human-readable component type name.

### [`scene.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene.h)

Root container for a loaded scene:
- Owns all root-level `SceneObject` instances via `std::unique_ptr`.
- Tracks pointers to the active primary `CameraComponent` and `DirectionalLightComponent`.
- Provides traversal helper queries:
  - `get_renderable_objects()`: Returns flat vector of objects containing a `MeshRenderer`.
  - `get_point_lights()`: Collects all active `PointLightComponent` instances.
  - `get_gi_probe_volumes()`: Collects active `GiProbeVolumeComponent` instances.
  - `get_reflection_probes()`: Collects active `ReflectionProbeComponent` instances.
  - `find_object(name)`: Depth-first search for an object by name.

### [`scene_object.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_object.h)

The core node in the scene tree (equivalent to Unity's `GameObject`):
- Holds object name, active state flag, non-owning parent pointer, list of owned components, and list of owned child objects.
- Component API: `add_component<T>(...)`, `get_component<T>()`, `attach_component(...)`.
- Convenient getters: `get_transform()`, `get_mesh_renderer()`, `get_animation()`.
- Hierarchy API: `add_child(...)`, `for_each_recursive(fn)`.
- Life-cycle methods (`start()`, `update(dt)`) recurse through attached components and active children.

### [`scene_manager.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_manager.h)

High-level manager for scene lifecycle:
- Wraps scene loading (`load_scene`) via `SceneLoader`.
- Owns the currently active `Scene`.
- Delegates frame updates (`update(delta_time)`) to the active scene.

### [`scene_loader.h`](file:///home/coopa/git/libcoopa/coopa/scene/scene_loader.h)

Parser and GPU asset loader for scene files:
- Supports both `.yaml` and `.caml` extensions using `caml::CAMLMap`.
- Parses Blender-exported YAML structures (`!Transform`, `!MeshRenderer`, `!Camera`, `!Light`, `!GiProbeVolume`, `!ReflectionProbe`, `!Animation`).
- Deduplicates and uploads GPU mesh resources (`coopa::gfx::engine::Mesh`) into a mesh cache.

---

## Component Submodule

For detailed documentation on individual component types, see the [Components Submodule README](file:///home/coopa/git/libcoopa/coopa/scene/components/README.md).

---

## Usage Example

```cpp
#include <coopa/scene/scene_manager.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/components/transform_component.h>
#include <coopa/scene/components/mesh_renderer.h>

// 1. Initialize scene manager
coopa::scene::SceneManager manager(device, allocator, command_pool);

// 2. Load scene from YAML description file
manager.load_scene("assets/scenes/gi_cornell_box/scene.yaml");

// 3. Obtain active scene
auto* scene = manager.get_active_scene();

// 4. Update scene hierarchy per frame
float delta_time = 0.016f; // 60 FPS frame delta
manager.update(delta_time);
```
