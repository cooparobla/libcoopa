# libcoopa

`libcoopa` is a high-performance C++ header-only utility library providing core runtime structures for parallel execution, configuration management, diagnostic logging, and mathematical operations. 

It is designed to be highly thread-safe and suitable for building multithreaded runtime systems.

## Modules Overview

`libcoopa` is organized into seven main modules:

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
- **[JobEngine](file:///home/coopa/git/libcoopa/coopa/job/engine.h)**: The execution manager. Spawns worker threads with lock-free Chase-Lev work-stealing deques, supports thread dedication by job type, and uses a spinning-then-sleeping strategy to minimize latency. Owns a pre-allocated `CounterPool` for zero-heap-allocation handle creation. Provides `begin_frame()` / `end_frame()` lifecycle for deterministic per-frame cleanup.
- **[JobScheduler](file:///home/coopa/git/libcoopa/coopa/job/scheduler.h)**: Dependency resolver that automatically calculates execution dependencies by analyzing data contention hazards: Read-After-Write (RAW), Write-After-Read (WAR), and Write-After-Write (WAW). Includes per-frame lifecycle to prevent unbounded tracking-state accumulation.
- **[JobHandle](file:///home/coopa/git/libcoopa/coopa/job/handle.h)**: Lightweight trivially-copyable token (16 bytes) referencing a counter in the engine's pre-allocated `CounterPool`. Replaces the previous `shared_ptr`-based design for zero-copy, zero-allocation tracking.
- **[Job](file:///home/coopa/git/libcoopa/coopa/job/job.h)**: Unit of work with a `TaskWrapper` (48-byte small-buffer-optimized callable) and fixed inline dependency storage (up to 4 handles, no heap allocation).
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
Scene graph, component model, and YAML scene loading — see the [module README](file:///home/coopa/git/libcoopa/coopa/scene/README.md):
- **[SceneManager](file:///home/coopa/git/libcoopa/coopa/scene/scene_manager.h)**, **[Scene](file:///home/coopa/git/libcoopa/coopa/scene/scene.h)**, **[SceneObject](file:///home/coopa/git/libcoopa/coopa/scene/scene_object.h)**, **[Component](file:///home/coopa/git/libcoopa/coopa/scene/component.h)**.
- **[SceneLoader](file:///home/coopa/git/libcoopa/coopa/scene/scene_loader.h)**: Parses YAML scenes, dispatching every non-Transform/Animation component to a parser registered from outside libcoopa (gfxcoopa, uicoopa, ...).

### 7. [Asset (`coopa/asset/`)](file:///home/coopa/git/libcoopa/coopa/asset/)
Generic, extensible asset system — see the [module README](file:///home/coopa/git/libcoopa/coopa/asset/README.md):
- **[AssetManager](file:///home/coopa/git/libcoopa/coopa/asset/asset_manager.h)**: Loader registration, refcounted caching, synchronous and async loading (via a dedicated `coopa::job::JobEngine`), mtime-polled hot reload, and shutdown.
- **[AssetHandle](file:///home/coopa/git/libcoopa/coopa/asset/asset_handle.h)** & **[AssetSlot](file:///home/coopa/git/libcoopa/coopa/asset/asset_slot.h)**: Typed refcounted references into address-stable slots, so a hot reload never invalidates a handle already handed out.
- **[IAssetLoader / TypedAssetLoader](file:///home/coopa/git/libcoopa/coopa/asset/asset_loader.h)**: The extension point downstream packages (gfxcoopa: mesh/texture/shader; uicoopa: sprite/font) register against, exactly as they already register scene components.

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