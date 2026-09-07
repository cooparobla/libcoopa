# Scene Components Submodule (`coopa::scene`)

This directory now holds only the one component type libcoopa's scene system
defines directly. Every renderer-specific component (mesh renderer, camera,
lights, GI/reflection probes) moved to gfxcoopa's `engine/components/`, and
every UI component (RectTransform, Canvas, Image, Text, Button, layout
groups, ...) lives in uicoopa's `uicoopa/`. Both register their component
YAML parsers with `coopa::scene::SceneLoader::register_component_parser()`
instead of this module knowing about them — see the parent
[Scene Module README](file:///home/coopa/git/libcoopa/coopa/scene/README.md).

The scene-wide `Animator` component that used to live here as the
orbit-only, single-purpose `AnimationComponent` has moved to its own sibling
module, `coopa::anim` — see
[Animation Module README](file:///home/coopa/git/libcoopa/coopa/animation/README.md).
It is registered the same way as any external component (via
`register_component_parser("Animator", ...)`, see
`coopa::anim::register_animation_components()`), not built into `SceneLoader`.

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
                                           ▼
                                   ┌───────────────┐
                                   │   Transform   │
                                   │   Component   │
                                   ├───────────────┤
                                   │ position      │
                                   │ rotation      │
                                   │ scale         │
                                   └───────────────┘

  (MeshRenderer, Camera, DirectionalLight, PointLight, EnvironmentLight,
   GiProbeVolume, ReflectionProbe -> gfxcoopa/gfxcoopa/engine/components/)
  (RectTransform, Canvas, Image, Text, Button, layout groups -> uicoopa/uicoopa/)
  (Animator -> coopa/animation/animator.h, registered via register_component_parser)
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
