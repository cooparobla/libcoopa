# Animation Module (`coopa::anim`)

The `animation` module provides a Unity-style scene animator: named clip
states, crossfade blending, keyframed and procedural tracks, and a
string-keyed property registry that lets an `Animator` drive fields on
component types libcoopa never names (uicoopa's `RectTransform`,
`Graphic::color`, a future gfxcoopa material property, ...) without a
virtual hook on `coopa::scene::Component`.

It is a sibling of `coopa::scene`, not part of it — `coopa/scene/README.md`
keeps that module free of any dependency beyond vendored fkYAML/glm and
libcoopa's own `coopa::event`/`coopa::job`, and this module needs
`coopa::asset::AssetHandle` (an `AnimationClip` is a pure-CPU asset) and
`coopa::job::JobEngine` (evaluation can be batched across a scene). Nothing
here includes gfxcoopa, uicoopa, or any other sibling repo — every
component-specific animatable field is registered from outside libcoopa via
`AnimatedPropertyRegistry`, the same extension pattern
`coopa::scene::SceneLoader::register_component_parser()` and
`coopa::asset::AssetManager::register_loader<T>()` already established.

---

## Architecture

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                            ANIMATION MODULE CLASS OVERVIEW                                │
└──────────────────────────────────────────────────────────────────────────────────────────┘

                    ┌────────────────────────┐
                    │  AnimatedPropertyRegistry │  (function-local static, per component_type.name)
                    ├────────────────────────┤
                    │ cast / get / set        │  <-- registered from OUTSIDE libcoopa
                    └────────────────────────┘        (uicoopa::register_ui_animated_properties())

                    ┌────────────────────────┐        ┌─────────────────────────┐
                    │   ProceduralTrackRegistry│      │      AnimationClip       │ (immutable, shared,
                    ├────────────────────────┤        ├─────────────────────────┤  AssetHandle<AnimationClip>)
                    │ orbit / sine / spin /...│◄───────│ tracks: AnimationTrack[] │
                    └────────────────────────┘        │   - keyframed: curve     │
                                                       │   - procedural: params   │
                                                       └────────────┬────────────┘
                                                                    │ resolved once (bind time)
                                                                    ▼
┌───────────────────────────────────────────────────────────────────────────────────────────┐
│  Animator : coopa::scene::Component                                                       │
│    states_        : AnimatorState[]     (name -> clip, inline or asset-loaded)             │
│    bindings_       : Binding[]          (Component*, AnimatedProperty*) deduped            │
│    current_/previous_ Layer             (time, crossfade weight)                           │
│    advance_() / evaluate_() / apply_()  <-- private; only AnimationSystem may call them     │
└───────────────────────────────────────────────────────────────────────────────────────────┘
                                                                    ▲ drives
                                                                    │
                    ┌───────────────────────────────────────────────────────────┐
                    │  AnimationSystem : coopa::scene::ISceneSystem              │
                    │  registered at UpdatePhase::Animation (300)                │
                    │  advance_ (serial) -> evaluate_ (parallel via JobEngine    │
                    │  once worth it) -> apply_ (serial)                         │
                    └───────────────────────────────────────────────────────────┘
```

---

## Why an `Animator` has no `update()`

Every other libcoopa component drives itself from `Component::update()`. An
`Animator` deliberately does not — it has no `update()`/`late_update()`
override at all. It is advanced only by a registered `AnimationSystem`, via a
`friend`-only `advance_()`/`evaluate_()`/`apply_()` split:

1. `advance_()` — serial, main thread: clocks, crossfade weights,
   `on_state_finished`, lazy rebinding, and snapshotting any procedural
   track's live `target_object` position (see below).
2. `evaluate_()` — samples every active track into a caller-owned scratch
   buffer. **Worker-safe by construction**: it touches no `Component`, no
   `SceneObject`, no `Scene` — only const clip data and the snapshot
   `advance_()` already wrote. `AnimationSystem` runs this inline, or as jobs
   across a `coopa::job::JobEngine` once a scene has enough active tracks to
   make that worthwhile (`AnimationSystem::set_parallel_threshold()`).
3. `apply_()` — serial, main thread: writes the accumulated result back onto
   every binding's target component.

This is what lets `AnimationSystem` batch evaluation across every `Animator`
in a scene into one job dispatch, instead of each `Animator` fighting for its
own place in the `Component::update()` order.

**Add `coopa::anim::install_animation_system(scene)` to any scene that uses
an `Animator`** — without it, `Animator::start()` still binds tracks and
`play()` still updates playback state, but nothing ever calls `advance_()`,
so nothing visibly animates.

---

## The `Scene` phase pipeline

`AnimationSystem` registers at `coopa::scene::UpdatePhase::Animation` (300),
which sits between the built-in `Behaviour` (200, the `Component::update()`
walk) and `LateBehaviour` (400, the `Component::late_update()` walk) — see
`coopa/scene/README.md`'s "Update Phases" section. Practically:

- A `Button::update()` calling `animator->crossfade(...)` takes effect the
  **same frame** (Behaviour runs before Animation).
- A UI `CanvasComponent::late_update()` (measure → arrange → emit) always
  sees the **final** animated pose (Animation runs before LateBehaviour).
- Two writers to the same property resolve deterministically by phase rather
  than by component-list order: a `Button`'s own color chase (phase 200) and an
  `Animator` track (phase 300) both targeting `Graphic::color` always resolve
  with the `Animator`'s write winning.

---

## Property binding: how an `Animator` touches `RectTransform`

`AnimatedPropertyRegistry` maps `(Component::type_name(), property name)` to
a typed `get`/`set`/`cast` triple. A downcast is resolved **once**, at an
`Animator`'s bind time, and cached — there is no per-frame RTTI.

Built in for `Transform`: `position`, `scale`, `rotation` (Euler degrees,
per-channel and deliberately not shortest-path, so 0 -> 720 spins twice) and
`rotation_quat` (x, y, z, w). A `rotation_quat` track interpolates channel by
channel and the setter normalises — nlerp, the short way round as long as
consecutive keys share a hemisphere (dot >= 0; the toyeditor's Timeline flips a
key's sign when recording to keep it so). Rigs key rotations this way: no
gimbal lock, no long way round.

```cpp
// uicoopa/ui_yaml.h — registered from OUTSIDE libcoopa
reg.register_vec<RectTransform, glm::vec2>("RectTransform", "anchored_position",
    &RectTransform::anchored_position, &RectTransform::set_anchored_position);

// Graphic::color is a member of the ABSTRACT base Graphic; register it once
// per concrete subclass key (Image, Text, ...) — the cast dynamic_casts to
// Graphic*, which succeeds for any of them.
reg.register_vec_member_as<Graphic, glm::vec4>("Image", "color", &Graphic::color);
```

A channel suffix (`.x`/`.y`/`.z`/`.w`, `.r`/`.g`/`.b`/`.a`, or a combination
like `.xy`) narrows a track to a subset of a property's channels —
`split_channel_suffix()` decodes it into a bitmask stored on the binding, not
the property. Two tracks resolving to the *same* `(Component*,
AnimatedProperty*)` — e.g. one driving `position.x` and another
`position.z`, or a crossfading previous/current pair on the same property —
are deduplicated into **one** shared accumulator (4 weighted-value slots + 4
weight slots). `apply_()` renormalizes by accumulated weight rather than each
track independently calling `set()`, which is what makes crossfade blending
and multi-track channel masking share one code path with no clobbering.

**This dedup is per-`Animator`, not scene-wide** — two *different* `Animator`s
targeting the same property still clobber each other by execution order.
`AnimationSystem::find_conflicts()` is a debug aid that lists every
`(object, component_type, property)` written by more than one `Animator`.

---

## Keyframed vs. procedural tracks

A track is either:

- **Keyframed** (`AnimationCurve`): a sorted list of `Keyframe { time, value[4], easing }`.
  `easing` (`Linear`, `Step`, `EaseIn`, `EaseOut`, `EaseInOut`) applies to the
  segment *leaving* that key. Sampling is stateless (`std::upper_bound`, no
  mutable cursor) because one clip's curve is sampled by many `Animator`s at
  once, potentially from worker threads.
- **Procedural** (`ProceduralTrack`): a string `type` (`orbit`, `sine`,
  `spin`, `constant`, or a consumer-registered type) plus a
  `ProceduralParams` bag, evaluated each frame by a pure function registered
  in `ProceduralTrackRegistry`. An evaluator **must be stateless and
  reentrant** — the same constraint as curve sampling, since evaluators also
  run inside `evaluate_()`.

Both kinds feed the *same* binding/blend path (see above), so a procedural
track and a keyframed track can crossfade against each other with zero
special-casing.

`orbit`'s `target_object` (an optional live-position input, replacing the
`center` parameter) is the one place a track needs *live* scene data. That
read happens in `advance_()`, on the main thread, and is snapshotted into a
per-track `ProceduralInputs` the worker-safe `evaluate_()` later reads —
never resolved from inside an evaluator itself.

A procedural track has no intrinsic length; it contributes nothing to
`AnimationClip::effective_length()`. A clip made up entirely of procedural
tracks needs an explicit `length:` (see the YAML schema below) — without one,
`effective_length()` is 0, which `Animator` treats as **infinite**: time is
never wrapped and `wrap:` is ignored, since there's nothing to wrap against.

---

## YAML schema

A clip is a standalone file (`type:`/flat-mapping form, never `!Tag` block
style — see `coopa/scene/scene_loader.h`'s documented fkYAML limitation):

```yaml
# assets/animations/banner_idle.yaml
clip:
  name: banner_idle
  wrap: loop            # loop | once | pingpong
  length: 2.4           # optional for keyframed clips; REQUIRED if every track is procedural
  tracks:
    - object: ""                     # "" or omitted = the Animator's own owner;
                                      # "A/B/C" walks descendants segment-by-segment;
                                      # any other name resolves via find_descendant(), then scene-wide.
      component: RectTransform       # optional; defaults to "Transform"
      component_index: 0             # optional; disambiguates two same-typed components on one object
      property: anchored_position.y  # AnimatedPropertyRegistry key, optional channel suffix
      keys:
        - { time: 0.0, value: -20.0, easing: ease_in_out }
        - { time: 1.2, value: -34.0, easing: ease_in_out }
        - { time: 2.4, value: -20.0 }

    - object: sphere.000              # procedural track -- no `keys:`
      property: position
      procedural:
        type: orbit
        target_object: cube.000       # live center, re-snapshotted every frame
        center: { x: -1.5, y: 0.0, z: 0.0 }   # fallback if target_object doesn't resolve
        radius: 2.7
        speed: 1.0
        height: 1.0
        initial_angle: 0.0
```

Key/parameter values accept a bare scalar, `{x,y,z,w}`/`{r,g,b,a}`, or a
`[a,b,c,d]` sequence — parsed into 4 floats pre-initialized to `{0,0,0,1}`
(the glm xyzw/rgba identity), so a color key omitting `a` gets `1.0` and a
vec2 key gets `0` in `z`. One clip per file — `AssetManager` enforces one C++
type per resolved path, so a multi-clip container would need a second asset
type.

An `Animator` component references one or more clips by name:

```yaml
- type: Animator
  auto_play: idle
  speed: 1.0
  default_crossfade: 0.15   # informational; crossfade() always takes an explicit duration
  states:
    - name: intro
      clip: ../animations/banner_intro.yaml
      wrap: once            # overrides the clip's own wrap for this state
    - name: idle
      clip: ../animations/banner_idle.yaml
      speed: 0.75           # overrides playback rate for this state
```

`"Animator"` is **not** built into `SceneLoader` — it is registered via
`register_animation_components(AssetManager&)`, alongside the
`AnimationClipLoader`. Trajectories are data, loaded through the asset system,
rather than anything hardcoded into the scene layer.

---

## Usage

```cpp
#include <coopa/animation/animation_yaml.h>
#include <coopa/animation/animation_system.h>

// Once, at startup:
coopa::anim::register_animation_components(assets); // registers AnimationClipLoader + "Animator" parser

coopa::scene::Scene scene = coopa::scene::SceneLoader::load("scene.yaml");
coopa::anim::install_animation_system(scene); // registers AnimationSystem at UpdatePhase::Animation

// Per frame:
scene.update(dt);       // Behaviour(200) -> Animation(300)
scene.late_update(dt);  // LateBehaviour(400) -- e.g. uicoopa's Canvas layout, sees the final pose

// Imperative control from anywhere with the Animator*:
animator->crossfade("hover", 0.15f);
```

For scaling evaluation across many `Animator`s onto a `coopa::job::JobEngine`:

```cpp
coopa::job::JobEngine engine(std::thread::hardware_concurrency());
scene.set_job_engine(&engine); // AnimationSystem evaluates large batches on its workers
```

The same engine can also be handed to `coopa::asset::AssetManager`; job
handles are reclaimed individually, so sharing it needs no coordination.

---

## Testing

Unit tests live in `libcoopa`'s top-level `test.cpp`, under the
`animation_test` namespace (curve sampling, property registry, binding
dedup/no-clobber, wrap modes, crossfade blending, name/path resolution,
lazy-bind-when-initially-inactive, rotation non-wrapping, YAML loading
including a malformed-file failure path, procedural-vs-keyframed blending,
the orbit evaluator against its closed-form formula, and a
serial-vs-forced-parallel bit-identical check). The `coopa::scene` phase
pipeline itself is covered separately, under `scene_pipeline_test`.
