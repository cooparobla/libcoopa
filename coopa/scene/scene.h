/**
 * @file scene.h
 * @brief Root of the scene hierarchy, owning root SceneObjects.
 *
 * A Scene contains the named root objects. It carries no knowledge of any
 * particular component type (camera, light, mesh renderer, ...) — those are
 * defined outside libcoopa (e.g. gfxcoopa's engine::components), and are
 * reached generically via get_components<T>() / find_first_component<T>().
 * update() processes all root objects (and their children recursively) and
 * can be dispatched as a job on a worker thread via libcoopa's JobEngine.
 */

#ifndef COOPA_SCENE_SCENE_H
#define COOPA_SCENE_SCENE_H

#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene_system.h>
#include <coopa/event/event_bus.h>
#include <coopa/job/engine.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include <algorithm>
#include <cstdint>

namespace coopa {
namespace scene {

/**
 * @class Scene
 * @brief Owns the root SceneObjects and provides generic hierarchy-wide queries.
 *
 * Constructed by SceneLoader (or built up manually via add_root_object()).
 * update()/late_update() run an ordered pipeline of ISceneSystem instances
 * (see scene_system.h) — the recursive Component::update()/late_update()
 * walks are just the two built-in systems (BehaviourSystem,
 * LateBehaviourSystem), auto-installed by every Scene at construction.
 * get_components<T>() / find_objects_with<T>() / find_first_component<T>()
 * give the render pipeline (or any other system) a flat view over whichever
 * component types it cares about, without Scene needing to know those types
 * itself.
 */
class Scene {
public:
    /**
     * @brief Constructs an empty scene with the given name.
     *
     * Installs the two built-in systems (BehaviourSystem at
     * UpdatePhase::Behaviour, LateBehaviourSystem at UpdatePhase::LateBehaviour)
     * so update()/late_update() behave exactly as they did before the system
     * pipeline existed, with nothing else registered.
     *
     * @param name Scene name (e.g. from the YAML scene_name field).
     */
    explicit Scene(std::string name = "Scene")
        : name_(std::move(name))
    {
        install_builtin_systems_();
    }

    // Non-copyable, movable. Move operations re-stamp every component's
    // Component::scene back-pointer to the NEW Scene address (see
    // restamp_scene_pointers_()) — without this, a Scene moved after start()
    // has run (e.g. SceneLoader::load()'s `return scene;`, then moved again
    // into SceneManager's unique_ptr<Scene>) would leave every component
    // pointing at a dangling, already-destroyed Scene. systems_/jobs_/
    // frame_index_/frame_open_ move alongside root_objects_/events_ for the
    // same reason: this Scene may be mid-frame the moment it is relocated.
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Scene(Scene&& other) noexcept
        : name_(std::move(other.name_)),
          root_objects_(std::move(other.root_objects_)),
          events_(std::move(other.events_)),
          systems_(std::move(other.systems_)),
          jobs_(other.jobs_),
          frame_index_(other.frame_index_),
          frame_open_(other.frame_open_)
    {
        other.jobs_ = nullptr;
        other.frame_open_ = false;
        restamp_scene_pointers_();
    }

    Scene& operator=(Scene&& other) noexcept {
        if (this == &other) return *this;
        name_         = std::move(other.name_);
        root_objects_ = std::move(other.root_objects_);
        events_       = std::move(other.events_);
        systems_      = std::move(other.systems_);
        jobs_         = other.jobs_;
        frame_index_  = other.frame_index_;
        frame_open_   = other.frame_open_;
        other.jobs_ = nullptr;
        other.frame_open_ = false;
        restamp_scene_pointers_();
        return *this;
    }

    // --- Accessors ---

    /** @brief Returns the scene name. */
    const std::string& name() const { return name_; }

    /**
     * @brief Adds a root SceneObject (takes ownership).
     * @param obj The root object to add.
     * @return Raw pointer to the added object.
     */
    SceneObject* add_root_object(std::unique_ptr<SceneObject> obj) {
        SceneObject* raw = obj.get();
        root_objects_.push_back(std::move(obj));
        return raw;
    }

    /** @brief Returns the list of root SceneObjects (read-only). */
    const std::vector<std::unique_ptr<SceneObject>>& root_objects() const {
        return root_objects_;
    }

    /** @brief Returns the list of root SceneObjects (mutable). */
    std::vector<std::unique_ptr<SceneObject>>& root_objects() {
        return root_objects_;
    }

    // --- Lifecycle ---

    /**
     * @brief Calls start() on all root objects (recursively).
     *
     * Should be called once after the scene is fully loaded. Also stamps
     * every component's Component::scene back-pointer to this Scene (just
     * before that component's own start() runs), so start()/update()/
     * late_update() can all reach scene->events() from within a component.
     */
    void start() {
        restamp_scene_pointers_();
        for (auto& obj : root_objects_) obj->start();
    }

    /**
     * @brief Runs every registered system with order < UpdatePhase::LateBehaviour.
     *
     * With nothing else registered this is just the built-in BehaviourSystem
     * (the recursive Component::update() walk), so existing callers see no
     * change. If a JobEngine was installed via set_job_engine(), this also
     * opens this frame's job boundary (calling begin_frame() on it) before
     * running any system — see set_job_engine()'s doc for why Scene, and only
     * Scene, is allowed to do that.
     *
     * NOTE: unlike before this pipeline existed, this is no longer safe to
     * call from a worker thread once a JobEngine is installed — it may act as
     * a guest worker inside a system's wait_for() call. With no engine
     * installed (the default), the old "safe from a worker thread as long as
     * the render thread isn't reading" guidance still holds.
     *
     * @param delta_time Frame delta time in seconds.
     */
    void update(float delta_time) {
        if (jobs_) {
            open_frame_();
        }
        FrameContext ctx{delta_time, frame_index_, jobs_};
        for (auto& entry : systems_) {
            if (entry.first < static_cast<int>(UpdatePhase::LateBehaviour)) {
                entry.second->execute(*this, ctx);
            }
        }
    }

    /**
     * @brief Runs every registered system with order >= UpdatePhase::LateBehaviour.
     *
     * With nothing else registered this is just the built-in
     * LateBehaviourSystem (the recursive Component::late_update() walk) —
     * call once per frame, after update(dt), exactly as before. Still valid
     * to call standalone with no preceding update() this frame (several
     * existing tests do): the job frame is only closed here if update()
     * actually opened one.
     *
     * @param delta_time Frame delta time in seconds.
     */
    void late_update(float delta_time) {
        FrameContext ctx{delta_time, frame_index_, jobs_};
        for (auto& entry : systems_) {
            if (entry.first >= static_cast<int>(UpdatePhase::LateBehaviour)) {
                entry.second->execute(*this, ctx);
            }
        }
        if (jobs_ && frame_open_) {
            jobs_->end_frame();
            frame_open_ = false;
        }
        ++frame_index_;
    }

    /**
     * @brief Stamps Component::scene on every component in obj's subtree,
     *        without calling start() on any of them.
     *
     * For attaching a subtree to this Scene after start() has already run
     * once (e.g. dynamically spawning a new SceneObject mid-game) — the
     * ordinary path (a subtree present at load time) is already covered by
     * start(). Idempotent; safe to call on a subtree that already belongs to
     * this Scene.
     *
     * @param obj Root of the subtree to adopt. Must already be reachable
     *            from this Scene's root_objects() (e.g. via add_root_object()
     *            or SceneObject::add_child()) — this only stamps the
     *            back-pointer, it does not change ownership.
     */
    void adopt(SceneObject& obj) {
        obj.for_each_recursive([this](SceneObject& o) {
            for (auto& comp : o.components()) comp->scene = this;
        });
    }

    /** @brief The scene-wide named EventBus — see coopa/event/event_bus.h. */
    coopa::event::EventBus& events() { return events_; }

    // --- System pipeline ---

    /**
     * @brief Installs the frame JobEngine (non-owning; the caller keeps ownership).
     *
     * CONTRACT: this Scene becomes the sole owner of that engine's frame
     * boundary — it calls begin_frame() at the top of update() (lazily
     * closing the previous frame first, so a caller that only ever calls
     * update() and never late_update() still gets exactly one open frame at
     * a time) and end_frame() at the bottom of late_update(). Do not pass an
     * engine whose begin_frame()/end_frame() the application also drives
     * itself, and never pass coopa::asset::AssetManager's own internal
     * engine — it is a separate instance with its own CounterPool and frame
     * lifetime, driven by AssetManager::update().
     *
     * Passing nullptr (the default state) makes every system run inline on
     * the calling thread — no threads are spawned unless this is called.
     *
     * @param engine Non-owning pointer to the JobEngine to drive, or nullptr.
     */
    void set_job_engine(coopa::job::JobEngine* engine) { jobs_ = engine; }

    /** @brief Returns the installed frame JobEngine, or nullptr if none. */
    coopa::job::JobEngine* job_engine() const { return jobs_; }

    /**
     * @brief Registers a system at the given phase slot.
     *
     * Systems at equal order run in registration order. Ownership transfers
     * to the Scene; on_attach() is called immediately.
     *
     * @return Non-owning pointer to the registered system.
     */
    ISceneSystem* add_system(std::unique_ptr<ISceneSystem> system, UpdatePhase phase) {
        return add_system(std::move(system), static_cast<int>(phase));
    }

    /**
     * @brief Registers a system at an arbitrary numeric order.
     *
     * Lets a consumer insert between the built-in phases (e.g. 350 for IK,
     * which runs after Animation(300) but still inside update() since it is
     * below LateBehaviour(400)).
     *
     * @return Non-owning pointer to the registered system.
     */
    ISceneSystem* add_system(std::unique_ptr<ISceneSystem> system, int order) {
        ISceneSystem* raw = system.get();
        systems_.emplace_back(order, std::move(system));
        std::stable_sort(systems_.begin(), systems_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        raw->on_attach(*this);
        return raw;
    }

    /**
     * @brief Removes the system with the given system_name(), calling on_detach().
     * @return True if a matching system was found and removed.
     */
    bool remove_system(const std::string& system_name) {
        auto it = std::find_if(systems_.begin(), systems_.end(),
            [&](const auto& entry) { return system_name == entry.second->system_name(); });
        if (it == systems_.end()) return false;
        it->second->on_detach(*this);
        systems_.erase(it);
        return true;
    }

    /** @brief Finds a registered system by system_name(), or nullptr. */
    ISceneSystem* find_system(const std::string& system_name) const {
        for (const auto& entry : systems_) {
            if (system_name == entry.second->system_name()) return entry.second.get();
        }
        return nullptr;
    }

    /** @brief Monotonically increasing frame counter, incremented once per late_update(). */
    uint64_t frame_index() const { return frame_index_; }

    // --- Generic queries ---

    /**
     * @brief Returns a flat list of every active component of type T in the scene hierarchy.
     * @tparam T Component type to search for.
     * @return Vector of non-owning T* pointers, in pre-order.
     */
    template<typename T>
    std::vector<T*> get_components() const {
        std::vector<T*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (auto* c = const_cast<SceneObject&>(obj).template get_component<T>()) {
                    result.push_back(c);
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns every active SceneObject that carries a component of type T.
     * @tparam T Component type to search for.
     * @return Vector of non-owning SceneObject pointers, in pre-order.
     */
    template<typename T>
    std::vector<SceneObject*> find_objects_with() const {
        std::vector<SceneObject*> result;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!obj.active()) return;
                if (obj.template get_component<T>()) {
                    result.push_back(const_cast<SceneObject*>(&obj));
                }
            });
        }
        return result;
    }

    /**
     * @brief Returns the first active component of type T found in the hierarchy (pre-order).
     * @tparam T Component type to search for.
     * @return Non-owning pointer to the first match, or nullptr.
     */
    template<typename T>
    T* find_first_component() const {
        T* found = nullptr;
        for (const auto& root : root_objects_) {
            root->for_each_recursive([&](const SceneObject& obj) {
                if (!found && obj.active()) {
                    found = const_cast<SceneObject&>(obj).template get_component<T>();
                }
            });
            if (found) return found;
        }
        return nullptr;
    }

    /**
     * @brief Finds the first SceneObject with the given name (depth-first).
     * @param name Name to search for.
     * @return Non-owning pointer to the found object, or nullptr.
     */
    SceneObject* find_object(const std::string& name) const {
        for (const auto& root : root_objects_) {
            if (root->name() == name) return root.get();
            if (SceneObject* found = root->find_descendant(name)) return found;
        }
        return nullptr;
    }

private:
    /**
     * @brief Stamps Component::scene = this on every component in the hierarchy.
     *
     * Regardless of active() — an initially-inactive object's components
     * should still be able to reach scene->events() once reactivated later
     * via set_active(true). Shared by start() and the move operations (a
     * moved-into Scene has a different address than the one that may have
     * already run start() before the move — see the move ctor/assignment doc).
     */
    void restamp_scene_pointers_() {
        for (auto& root : root_objects_) {
            root->for_each_recursive([this](SceneObject& obj) {
                for (auto& comp : obj.components()) comp->scene = this;
            });
        }
    }

    /** @brief Installs BehaviourSystem/LateBehaviourSystem. Called once, from every constructor path. */
    void install_builtin_systems_() {
        add_system(std::make_unique<BehaviourSystem>(), UpdatePhase::Behaviour);
        add_system(std::make_unique<LateBehaviourSystem>(), UpdatePhase::LateBehaviour);
    }

    /**
     * @brief Opens this frame's job boundary, closing the previous one first if needed.
     *
     * A caller that only ever calls update() and never late_update() (e.g.
     * blendy, toyengine) never runs the end_frame() at the bottom of
     * late_update(), so the previous frame's boundary is still "open" the
     * next time update() runs — close it here before opening a new one, so
     * exactly one frame is ever open at a time regardless of which entry
     * points the caller uses.
     */
    void open_frame_() {
        if (frame_open_) jobs_->end_frame();
        jobs_->begin_frame();
        frame_open_ = true;
    }

    std::string                               name_;         /**< Scene name. */
    std::vector<std::unique_ptr<SceneObject>> root_objects_; /**< Owned root objects. */
    coopa::event::EventBus                    events_;       /**< Scene-wide named signal bus. */

    /** @brief Registered systems, kept sorted ascending by order; equal orders keep insertion order. */
    std::vector<std::pair<int, std::unique_ptr<ISceneSystem>>> systems_;
    coopa::job::JobEngine* jobs_        = nullptr; /**< Non-owning. See set_job_engine(). */
    uint64_t                frame_index_ = 0;      /**< Bumped once per late_update(). */
    bool                    frame_open_  = false;  /**< Whether jobs_->begin_frame() has an unmatched end_frame(). */
};

// Defined out-of-line, after Scene is a complete type — these are the two
// built-in ISceneSystem bodies declared in scene_system.h.

inline void BehaviourSystem::execute(Scene& scene, const FrameContext& ctx) {
    for (auto& obj : scene.root_objects()) obj->update(ctx.delta_time);
}

inline void LateBehaviourSystem::execute(Scene& scene, const FrameContext& ctx) {
    for (auto& obj : scene.root_objects()) obj->late_update(ctx.delta_time);
}

} // namespace scene
} // namespace coopa

#endif // COOPA_SCENE_SCENE_H
