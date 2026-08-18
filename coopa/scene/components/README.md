# Scene Components Submodule (`coopa::scene`)

This directory now holds only the two component types libcoopa's scene system
defines directly. Every renderer-specific component (mesh renderer, camera,
lights, GI/reflection probes) moved to gfxcoopa's `engine/components/`, and
every UI component (RectTransform, Canvas, Image, Text, Button, layout
groups, ...) lives in uicoopa's `uicoopa/`. Both register their component
YAML parsers with `coopa::scene::SceneLoader::register_component_parser()`
instead of this module knowing about them — see the parent
[Scene Module README](file:///home/coopa/git/libcoopa/coopa/scene/README.md).

---

## Component Inheritance Graph

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                            COMPONENT INHERITANCE HIERARCHY                               │
└──────────────────────────────────────────────────────────────────────────────────────────┘
                                           │
                                ┌──────────┴──────────┐
                                │      Component      │
                                │     (Abstract)      │
                                └──────────┬──────────┘
                                           │
                         ┌─────────────────┴─────────────────┐
                         ▼                                    ▼
                 ┌───────────────┐                   ┌───────────────┐
                 │   Transform   │                   │   Animation   │
                 │   Component   │                   │   Component   │
                 ├───────────────┤                   ├───────────────┤
                 │ position      │                   │ orbit target  │
                 │ rotation      │                   │ radius/speed  │
                 │ scale         │                   │ elapsed_time  │
                 └───────────────┘                   └───────────────┘

  (MeshRenderer, Camera, DirectionalLight, PointLight, EnvironmentLight,
   GiProbeVolume, ReflectionProbe -> gfxcoopa/gfxcoopa/engine/components/)
  (RectTransform, Canvas, Image, Text, Button, layout groups -> uicoopa/uicoopa/)
```

---

## Component Breakdown

### [`transform_component.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/transform_component.h)

Wraps `coopa::util::Transform`, linking it into the SceneObject hierarchy for
world-matrix propagation:
- `transform()`: Mutable/const access to the underlying `coopa::util::Transform`
  (`position`, `rotation_degrees`, `scale`, local/world matrices).
- `get_world_matrix() -> glm::mat4`: Lazily recomputed from the parent chain.
- `set_parent_transform(...)`: Links this transform under a parent's transform.

### [`animation_component.h`](file:///home/coopa/git/libcoopa/coopa/scene/components/animation_component.h)

Procedural per-frame transform animation, loadable from its own YAML file:
- `type`: Currently `AnimationType::Orbit` (horizontal circular orbit).
- `target_object` / `center`: Orbit pivot, either a named object or a fixed point.
- `radius`, `speed`, `height`, `initial_angle`: Orbit parameters.
- `load_from_yaml(path)`: Parses these fields from a standalone animation YAML file.
