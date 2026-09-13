# libcoopa

`libcoopa` is a high-performance C++ header-only utility library providing core runtime structures for parallel execution, configuration management, diagnostic logging, and mathematical operations. 

It is designed to be highly thread-safe and suitable for building multithreaded runtime systems.

## Modules Overview

`libcoopa` is organized into twelve main modules:

### 1. [Utilities (`coopa/util/`)](file:///home/coopa/git/libcoopa/coopa/util/)
Cross-platform helper utilities for math, file paths, strings, and unique IDs:
- **[IdUtil](file:///home/coopa/git/libcoopa/coopa/util/id.h)**: Thread-safe atomic counter for generating unique runtime identifiers.
- **[StringUtil](file:///home/coopa/git/libcoopa/coopa/util/string.h)**: Common header-only string operations like splitting, joining, case conversions, and substring replacement.
- **[MathUtil & Mat4](file:///home/coopa/git/libcoopa/coopa/util/math.h)**: Matrix mathematics featuring a lightweight 4x4 matrix representation ([Mat4](file:///home/coopa/git/libcoopa/coopa/util/math.h)) optimized for column-major layouts, transformations, and interoperability with `glm::mat4`.
- **[FileUtil](file:///home/coopa/git/libcoopa/coopa/util/file.h)**: Path resolvers for assets and project/root relative file locations.

### 2. [Collections (`coopa/collections/`)](file:///home/coopa/git/libcoopa/coopa/collections/)
Structures for configuration parsing and structured serialization:
- **[YAMLMap](file:///home/coopa/git/libcoopa/coopa/collections/yaml_map.h)**: A wrapper encapsulating `fkYAML` nodes to ease loading, mutating, querying, and serializing YAML configurations.

### 3. [Job System (`coopa/job/`)](file:///home/coopa/git/libcoopa/coopa/job/)
A high-performance concurrent scheduling engine optimized for realtime/game-engine frame loops. Features lock-free work-stealing, zero-allocation job submission, and automatic hazard-based dependency resolution:
- **[JobEngine](file:///home/coopa/git/libcoopa/coopa/job/engine.h)**: The execution manager. Spawns worker threads with lock-free Chase-Lev work-stealing deques, supports thread dedication by job type, and uses a spinning-then-sleeping strategy to minimize latency. Owns a pre-allocated `CounterPool` for zero-heap-allocation handle creation, and a `DependencyGraph` that resolves dependencies event-driven. `begin_frame()` / `end_frame()` reset per-frame diagnostic counters only.
- **[JobScheduler](file:///home/coopa/git/libcoopa/coopa/job/scheduler.h)**: Dependency resolver that automatically calculates execution dependencies by analyzing data contention hazards: Read-After-Write (RAW), Write-After-Read (WAR), and Write-After-Write (WAW). Includes per-frame lifecycle to prevent unbounded tracking-state accumulation.
- **[JobHandle](file:///home/coopa/git/libcoopa/coopa/job/handle.h)**: Lightweight trivially-copyable token (16 bytes) referencing a generation-tagged slot in the engine's pre-allocated `CounterPool`, for zero-copy, zero-allocation completion tracking. Slots are reclaimed individually, so a handle's lifetime is independent of any frame boundary.
- **[Job](file:///home/coopa/git/libcoopa/coopa/job/job.h)**: Unit of work with a `TaskWrapper` (48-byte small-buffer-optimized callable). Carries no dependency storage of its own — a job with dependencies is parked in a `PendingNode` by the `DependencyGraph` until they are met.
- **[DependencyGraph](file:///home/coopa/git/libcoopa/coopa/job/dependency_graph.h)**: Event-driven, unbounded-fan-in dependency resolution. Each dependent registers a waiter on its dependency's own lock-free stack, so completion fires dependents directly instead of anyone scanning or polling.
- **[Platform](file:///home/coopa/git/libcoopa/coopa/job/platform.h)**: Compile-time platform detection (x86-64 vs ARM/Apple Silicon cache line sizes) and job system configuration constants.
- **Concurrent Containers (`coopa/job/collections/`)**:
  - **[WorkStealingDeque](file:///home/coopa/git/libcoopa/coopa/job/collections/work_stealing_deque.h)**: Lock-free Chase-Lev work-stealing deque. Owner pushes/pops from bottom (zero contention), thieves steal from top (single CAS). Cache-line-padded indices prevent false sharing.
  - **[ParallelQueue](file:///home/coopa/git/libcoopa/coopa/job/collections/queue.h)**: Thread-safe FIFO queue.
  - **[ParallelVector](file:///home/coopa/git/libcoopa/coopa/job/collections/vector.h)**: Thread-safe vector wrapper.
  - **[ParallelMap](file:///home/coopa/git/libcoopa/coopa/job/collections/map.h)**: Thread-safe hash map wrapping `phmap`.

### 4. [Diagnostics & Logging (`coopa/debug/`)](file:///home/coopa/git/libcoopa/coopa/debug/)
Thread-safe logs and diagnostics capturing tools:
- **[Logger](file:///home/coopa/git/libcoopa/coopa/debug/logger.h)**: Thread-safe synchronous console log printer formatting severity level, module tag, and timestamp with microsecond resolution.
- **[DebugManager](file:///home/coopa/git/libcoopa/coopa/debug/manager.h)** & **[DebugBucket](file:///home/coopa/git/libcoopa/coopa/debug/bucket.h)**: Thread-safe queue buffers collecting parallel logs, sorting them chronologically, and flushing.

### 5. [Events (`coopa/event/`)](file:///home/coopa/git/libcoopa/coopa/event/)
Header-only multicast dispatch:
- **[Signal](file:///home/coopa/git/libcoopa/coopa/event/signal.h)**: `Signal<Args...>` with RAII `Connection`/`ScopedConnection` tokens, safe re-entrant connect/disconnect during `emit()`.
- **[EventBus](file:///home/coopa/git/libcoopa/coopa/event/event_bus.h)**: Named pub/sub over a dynamic `EventArgs` bag, with wildcard listeners.

### 6. [Scene (`coopa/scene/`)](file:///home/coopa/git/libcoopa/coopa/scene/)
Scene graph, component model, an ordered per-frame system pipeline, and YAML scene loading — see the [module README](file:///home/coopa/git/libcoopa/coopa/scene/README.md):
- **[SceneManager](file:///home/coopa/git/libcoopa/coopa/scene/scene_manager.h)**, **[Scene](file:///home/coopa/git/libcoopa/coopa/scene/scene.h)**, **[SceneObject](file:///home/coopa/git/libcoopa/coopa/scene/scene_object.h)**, **[Component](file:///home/coopa/git/libcoopa/coopa/scene/component.h)**.
- **[ISceneSystem / UpdatePhase](file:///home/coopa/git/libcoopa/coopa/scene/scene_system.h)**: `Scene::update()`/`late_update()` run an ordered list of systems (the recursive `Component::update()`/`late_update()` walks are just the two built-ins) — the seam `coopa::anim::AnimationSystem` registers into, and where a future physics system would too. `Scene::set_job_engine()` lets a system dispatch its work as jobs on an engine that may be shared with any other subsystem.
- **[SceneLoader](file:///home/coopa/git/libcoopa/coopa/scene/scene_loader.h)**: Parses YAML scenes, dispatching every non-Transform component to a parser registered from outside libcoopa (gfxcoopa, uicoopa, coopa::anim, ...).

### 7. [Asset (`coopa/asset/`)](file:///home/coopa/git/libcoopa/coopa/asset/)
Generic, extensible asset system — see the [module README](file:///home/coopa/git/libcoopa/coopa/asset/README.md):
- **[AssetManager](file:///home/coopa/git/libcoopa/coopa/asset/asset_manager.h)**: Loader registration, refcounted caching, synchronous and async loading (on a supplied or privately-owned `coopa::job::JobEngine`), mtime-polled hot reload, and shutdown.
- **[AssetHandle](file:///home/coopa/git/libcoopa/coopa/asset/asset_handle.h)** & **[AssetSlot](file:///home/coopa/git/libcoopa/coopa/asset/asset_slot.h)**: Typed refcounted references into address-stable slots, so a hot reload never invalidates a handle already handed out.
- **[IAssetLoader / TypedAssetLoader](file:///home/coopa/git/libcoopa/coopa/asset/asset_loader.h)**: The extension point downstream packages (gfxcoopa: mesh/texture/shader; uicoopa: sprite/font; coopa::anim: AnimationClip) register against, exactly as they already register scene components.

### 8. [Animation (`coopa/animation/`)](file:///home/coopa/git/libcoopa/coopa/animation/)
Unity-style scene animator — see the [module README](file:///home/coopa/git/libcoopa/coopa/animation/README.md):
- **[Animator](file:///home/coopa/git/libcoopa/coopa/animation/animator.h)**: A component that plays named `AnimatorState`s (each an `AnimationClip`), crossfading between them, driving any number of `AnimatedProperty` bindings on any component type — including ones libcoopa never names (uicoopa's `RectTransform`, `Graphic::color`).
- **[AnimationClip](file:///home/coopa/git/libcoopa/coopa/animation/animation_clip.h)**: Immutable, YAML-loadable animation data — keyframed tracks (linear + eased interpolation) and procedural tracks (formula-driven, e.g. `orbit`/`sine`/`spin`), both sharing one binding/blend path.
- **[AnimatedPropertyRegistry](file:///home/coopa/git/libcoopa/coopa/animation/animated_property.h)**: The extension point that lets any component type register animatable fields, mirroring `SceneLoader::register_component_parser()`.
- **[AnimationSystem](file:///home/coopa/git/libcoopa/coopa/animation/animation_system.h)**: The `ISceneSystem` that drives every `Animator` in a scene, batching evaluation across a `JobEngine` once the workload is large enough to be worth it.

### 9. [Input (`coopa/input/`)](file:///home/coopa/git/libcoopa/coopa/input/)
The complete keyboard/mouse vocabulary and state model, backend-agnostic — see the [module README](file:///home/coopa/git/libcoopa/coopa/input/README.md):
- **[Input](file:///home/coopa/git/libcoopa/coopa/input/input.h)**: Owns all key/button level state, edges, held time, deltas, and discrete events; fed by a backend's `push_*()` calls (e.g. gfxcoopa's GLFW-backed `presentation::Window`) and queried directly by application code.
- **[InputMap](file:///home/coopa/git/libcoopa/coopa/input/input_map.h)**: Named action/axis/vector bindings over keys and mouse buttons, with optional modifier chords, resolved against an `Input`.
- **[IInputBackend](file:///home/coopa/git/libcoopa/coopa/input/input_backend.h)**: The interface a concrete windowing library implements so `Input`'s cursor/clipboard control calls have somewhere to go.
- **[Key / MouseButton / Mods / ...](file:///home/coopa/git/libcoopa/coopa/input/keys.h)**: The dense, 0-based, backend-independent key/button vocabulary.

### 10. [Item (`coopa/item/`)](file:///home/coopa/git/libcoopa/coopa/item/)
The engine-agnostic model behind any slot-based inventory UI — see the [module README](file:///home/coopa/git/libcoopa/coopa/item/README.md):
- **[ItemId / ItemDef / ItemDatabase](file:///home/coopa/git/libcoopa/coopa/item/item_database.h)**: Normalized, hashable item identity; the immutable per-kind definition (name/icon/max_stack/category/rarity/tint); and the `ItemId -> ItemDef` lookup table, definable from C++ or YAML side by side.
- **[ItemDatabaseLoader](file:///home/coopa/git/libcoopa/coopa/item/item_database_loader.h)**: A `TypedAssetLoader<ItemDatabase>` parsing a YAML `items:` list, mirroring `coopa::anim::AnimationClipLoader`'s pure-CPU shape.
- **[Inventory](file:///home/coopa/git/libcoopa/coopa/item/inventory.h)**: Fixed-capacity `ItemStack` storage with the authoritative move/merge/swap/split rules and change signals — the model a UI grid visualizes rather than owns.
- **[Hotbar](file:///home/coopa/git/libcoopa/coopa/item/hotbar.h)**: A selectable window over an `Inventory`'s slots, e.g. a quick-slot bar.

### 11. [Stat (`coopa/stat/`)](file:///home/coopa/git/libcoopa/coopa/stat/)
Clamped, optionally-regenerating gameplay quantities (health, stamina, mana) — see the [module README](file:///home/coopa/git/libcoopa/coopa/stat/README.md):
- **[Resource](file:///home/coopa/git/libcoopa/coopa/stat/resource.h)**: A single current/max meter with `damage`/`heal`/`tick`-driven delayed regen, `on_changed`/`on_depleted` signals.
- **[StatBlock](file:///home/coopa/git/libcoopa/coopa/stat/stat_block.h)**: A named, pointer-stable registry of `Resource`s.

### 12. [Maps (`coopa/maps/`)](file:///home/coopa/git/libcoopa/coopa/maps/)
Seeded Voronoi world generation — climate, biomes, rivers, roads, nations, settlements and landmarks — see the [module README](file:///home/coopa/git/libcoopa/coopa/maps/README.md):
- **[MapGenerator](file:///home/coopa/git/libcoopa/coopa/maps/map_generator.h)**: Builds the Delaunay/Voronoi dual graph from a jittered point lattice and drives the generation passes over it.
- **[MapGraph](file:///home/coopa/git/libcoopa/coopa/maps/map_data.h)**: The generated world — index-addressed cells, corners, edges, towns, regions, countries and landmarks.
- **[Passes](file:///home/coopa/git/libcoopa/coopa/maps/passes/README.md)**: Twelve ordered annotation stages: water, coast, elevation, temperature, rivers, moisture, biomes, roads, regions, towns, landmarks, noisy edges.
- **[Biomes](file:///home/coopa/git/libcoopa/coopa/maps/biome.h) & [names](file:///home/coopa/git/libcoopa/coopa/maps/name_generator.h)**: 33 biomes classified on temperature × elevation × moisture, and a per-region synthetic language that names every place.
- **[map_yaml](file:///home/coopa/git/libcoopa/coopa/maps/map_yaml.h) & [renderers](file:///home/coopa/git/libcoopa/coopa/maps/map_renderer.h)**: Full-graph YAML save/load, plus software biome and elevation renders written out as PNG.

---

## Building and Running Tests

The library is built as part of the `trav` test runner target using CMake.

### Build the Project
Configure and compile using the custom compiler shortcut:
```bash
cbuild
```

### Run Unit Tests
Run the test runner to execute the test suite (verifying all core modules) using:
```bash
cplay
```
The test suite source is located in [test.cpp](file:///home/coopa/git/libcoopa/test.cpp).