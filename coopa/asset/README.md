# Asset Module (`coopa::asset`)

The `asset` module provides a generic, extensible asset system: identity,
path resolution, refcounted caching, two-stage (decode/finalize) loading —
synchronous or asynchronous via a dedicated `coopa::job::JobEngine` — and
mtime-polled hot reload. It is **dependency-free**: nothing in this module
includes gfxcoopa, caml, uicoopa, or any other sibling repo — only libcoopa's
own `coopa::job` and `coopa::event` modules. Every concrete asset type (mesh,
texture, shader, sprite, font, audio clip, ...) lives outside libcoopa and is
registered via `AssetManager::register_loader<T>()`, never by this module
directly — the same extension pattern `coopa::scene::SceneLoader` already
established for scene components.

---

## Asset Module Architecture

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                              ASSET MODULE CLASS HIERARCHY                                 │
└──────────────────────────────────────────────────────────────────────────────────────────┘

                                ┌───────────────────────┐
                                │      AssetManager      │
                                ├───────────────────────┤
                                │ AssetSource            │
                                │ JobEngine (dedicated)  │
                                │ loaders_  : type -> T  │
                                │ slots_    : hash -> T  │
                                │ pending_  : in-flight  │
                                └───────────┬───────────┘
                                            │ owns
                                            ▼
                                ┌───────────────────────┐
                                │       AssetSlot        │ (address-stable, detail::)
                                ├───────────────────────┤
                                │ AssetId, AssetState    │
                                │ ref_count, revision    │
                                │ payload : shared_ptr   │
                                └───────────┬───────────┘
                                            │ referenced by (non-owning)
                                            ▼
                                ┌───────────────────────┐
                                │     AssetHandle<T>     │  (held by application code)
                                └───────────────────────┘

                                ┌───────────────────────┐
                                │      IAssetLoader      │  (defined outside libcoopa)
                                ├───────────────────────┤
                                │ decode()   -- worker   │
                                │ finalize() -- main     │
                                └───────────────────────┘
```

---

## Loading Sequence

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                                  ASSET LOADING LIFECYCLE                                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘

  Application          AssetManager           IAssetLoader          Worker thread
      │                     │                       │                     │
      │── register_loader<T>(...) for every asset type this application needs
      │                     │                       │                     │
      │── load_async<T>(path) ─────►│               │                     │
      │                     │── create_handle() (dedicated JobEngine)     │
      │                     │── submit(decode) ─────────────────────────►│
      │                     │                       │── decode() ─────────┤ (CPU only, no GPU)
      │◄── AssetHandle<T> (state = Loading) ─────────│                     │
      │                     │                       │                     │
      │── update(dt) every frame ──►│                                     │
      │                     │◄── handle.is_complete() ───────────────────│
      │                     │── finalize() (main thread, GPU-safe) ──────►│
      │                     │── publish payload, bump revision            │
      │◄── on_reloaded (only if this was a reload) ──│                     │
```

---

## Structural Specifications & Ownership Matrix

| Class | Primary Owner | Contained Members | Responsibility |
|---|---|---|---|
| `AssetManager` | Application | `AssetSource`, dedicated `JobEngine`, slot/loader/pending maps | Facade: registration, load/load_async, per-frame `update()`, hot reload, shutdown |
| `AssetSource` | `AssetManager` | Search-root list | Resolves virtual paths to filesystem paths; reads bytes and mtimes |
| `AssetSlot` (`detail::`) | `AssetManager` | `AssetId`, state, refcount, revision, type-erased payload | Address-stable, per-asset storage; hot reload swaps the payload in place |
| `AssetHandle<T>` | Application code | Non-owning `AssetSlot*` | Typed, refcounted reference; stays valid across a hot reload |
| `IAssetLoader` / `TypedAssetLoader<T, I>` | Registered on `AssetManager` | — | Two-stage decode (worker-safe)/finalize (main-thread, GPU-safe) for one asset type |

---

## File Breakdown

### [`asset_id.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_id.h)

`AssetId::from_path()` normalizes a virtual path (backslashes, leading `./`)
and hashes it (FNV-1a 64-bit) — the key every slot is stored under.
`AssetManager::load()`/`load_async()`/`get()` build the id from the
*resolved* path (after `AssetSource::resolve()` has applied `base_dir` and
the search roots), not the raw string passed in — otherwise two different
scene directories referencing the same relative filename (e.g. two scenes
each with their own `meshes/cube.000.yaml`) would collide into one cached
slot. Existing YAML/scene files keep referencing assets by path; nothing about asset data
itself needs to change to adopt this system.

### [`asset_state.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_state.h)

`enum class AssetState { Unloaded, Loading, Loaded, Failed }` — a slot's
lifecycle. A `Loaded` slot can cycle back to `Loading`/`Loaded` via hot
reload without ever returning to `Unloaded`.

### [`asset_slot.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_slot.h)

`detail::AssetSlot` — type-erased (`std::shared_ptr<void> payload`),
address-stable storage owned by `AssetManager` in a `unique_ptr` map. Because
the address never moves, hot reload can swap `payload` and bump `revision`
in place without invalidating any `AssetHandle<T>` that already exists.

### [`asset_handle.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_handle.h)

`AssetHandle<T>` — an 8-byte-pointer-sized, refcounted, typed reference.
`is_loaded()`, `get()`/`operator->`, `state()`, `error()`, and `revision()`
(bumped on every publish, including the first — compare against a cached
value to detect a reload happened). Main-thread only, like every other
GPU-adjacent type in this workspace.

### [`asset_loader.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_loader.h)

`IAssetLoader` — the type-erased two-stage interface: `decode()` (worker-safe,
CPU-only) then `finalize()` (main-thread, GPU-safe). `TypedAssetLoader<T,
Intermediate>` is the adapter downstream loaders should actually subclass —
it removes `std::shared_ptr<void>` casts from the implementation.

### [`asset_source.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_source.h)

`AssetSource` — search-root path resolution (`add_search_root`, `resolve`,
`exists`) plus static `read_bytes`/`last_write_time_ns` helpers. Deliberately
supersedes `coopa::util::FileUtil` (global-namespace legacy with a live
substring-length bug and no users outside libcoopa's own tests) rather than
building on it.

### [`asset_manager.h`](file:///home/coopa/git/libcoopa/coopa/asset/asset_manager.h)

`AssetManager` — the facade: `register_loader<T>`, `load<T>` (synchronous),
`load_async<T>`, `get<T>` (lookup without loading), `create<T>` (publish an
already-built payload for a procedurally-generated asset, bypassing
`AssetSource`/loaders entirely), `unload`/`garbage_collect` (immediate,
explicit), `set_idle_eviction`/`set_max_idle_frames` (automatic, opt-in —
evicts anything unreferenced for a configurable number of `update()` calls),
`set_hot_reload`/`set_poll_interval`, `on_reloaded` (a `coopa::event::Signal`),
`update(delta_time)` (call once per frame), and `shutdown()`.

Three design points worth knowing before extending this class:

- **Its `JobEngine` is dedicated, never the application's own per-frame one.**
  `coopa::job::CounterPool::reset()` is a bare bump-pointer reset with no
  generation tag, so a `JobHandle` held across a `begin_frame()` would
  silently alias a recycled slot. `AssetManager::update()` only resets its
  engine's pool once no loads are pending — the one point that is provably
  safe.
- **A path is bound to exactly one C++ type.** `AssetId` is purely
  path-based; loading the same path as two different types would otherwise
  let the second load reinterpret the first type's payload. `AssetManager`
  guards against this — a mismatched `load<T>`/`load_async<T>`/`get<T>`
  fails loudly (logged, empty handle returned) rather than risking undefined
  behavior.
- **Every eviction path shares one grace period.** `unload()`,
  `garbage_collect()`, idle eviction, `create()` re-publishing, and hot
  reload all funnel through the same private `retire_payload_()` — a
  superseded payload is held on a retire list for `k_payload_grace_frames`
  frames before actual destruction, never destroyed synchronously inside the
  call that dropped it. This matters even for the "immediate" calls
  (`unload`/`garbage_collect`): a payload's last reference can be dropped
  the same frame a command buffer that still reads it was submitted, so
  destroying it inline would be a use-after-free on the GPU.

---

## Usage Example

```cpp
#include <coopa/asset/asset_manager.h>

// Defined by a downstream package (gfxcoopa, uicoopa, ...), not by libcoopa:
class TextureLoader : public coopa::asset::TypedAssetLoader<Texture, DecodedImage> {
public:
    std::shared_ptr<DecodedImage> decode_typed(const coopa::asset::AssetId&,
                                                const coopa::asset::LoadContext& ctx) override {
        // Worker-thread safe: read + decode pixels only, no GPU calls.
        return decode_image(coopa::asset::AssetSource::read_bytes(ctx.resolved_path));
    }
    std::shared_ptr<Texture> finalize_typed(std::shared_ptr<DecodedImage> decoded,
                                             const coopa::asset::AssetId&,
                                             const coopa::asset::LoadContext&) override {
        return std::make_shared<Texture>(upload_to_gpu(device_, allocator_, *decoded));
    }
    const char* type_name() const override { return "Texture"; }
};

coopa::asset::AssetManager assets;
assets.add_search_root(std::string(ROOT_DIR) + "/assets");
assets.register_loader<Texture>(std::make_unique<TextureLoader>(device, allocator, cmd_pool));
assets.set_hot_reload(true);

coopa::asset::AssetHandle<Texture> tex = assets.load_async<Texture>("textures/brick.png");

// Per frame:
assets.update(delta_time);
if (tex.is_loaded()) {
    bind_texture(*tex);
}

// Before Device/Allocator are destroyed:
assets.shutdown();
```
