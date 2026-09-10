#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <cstdio> // For std::remove
#include <fstream>
#include <filesystem>
#include <unordered_map>

#include <coopa/util/id.h>
#include <coopa/util/string.h>
#include <coopa/util/math.h>
#include <coopa/util/file.h>
#include <coopa/collections/yaml_map.h>
#include <coopa/debug/message.h>
#include <coopa/debug/printer.h>
#include <coopa/debug/logger.h>
#include <coopa/debug/context.h>
#include <coopa/debug/manager.h>
#include <coopa/debug/bucket.h>
#include <coopa/job/collections/queue.h>
#include <coopa/job/collections/vector.h>
#include <coopa/job/collections/map.h>
#include <coopa/job/collections/work_stealing_deque.h>
#include <coopa/job/job.h>
#include <coopa/job/handle.h>
#include <coopa/job/context.h>
#include <coopa/job/dependency_graph.h>
#include <coopa/job/thread.h>
#include <coopa/job/engine.h>
#include <coopa/job/parallel_for.h>
#include <coopa/job/scheduler.h>
#include <coopa/event/signal.h>
#include <coopa/asset/asset_manager.h>
#include <coopa/scene/component.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_system.h>
#include <coopa/scene/scene_commands.h>
#include <coopa/scene/scene_loader.h>
#include <coopa/scene/scene_manager.h>
#include <coopa/scene/systems/transform_system.h>
#include <coopa/animation/keyframe.h>
#include <coopa/animation/animation_curve.h>
#include <coopa/animation/animation_clip.h>
#include <coopa/animation/animated_property.h>
#include <coopa/animation/procedural_track.h>
#include <coopa/animation/animator.h>
#include <coopa/animation/animation_system.h>
#include <coopa/animation/animation_clip_loader.h>
#include <coopa/animation/animation_yaml.h>
#include <coopa/input/keys.h>
#include <coopa/input/input.h>
#include <coopa/input/input_map.h>
#include <coopa/item/item_id.h>
#include <coopa/item/item_def.h>
#include <coopa/item/item_stack.h>
#include <coopa/item/item_database.h>
#include <coopa/item/item_database_loader.h>
#include <coopa/item/inventory.h>
#include <coopa/item/hotbar.h>
#include <coopa/stat/resource.h>
#include <coopa/stat/stat_block.h>
#include <glm/glm.hpp>

// ANSI Colors for nice UI
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define RUN_TEST(test_func) \
    do { \
        std::cout << ANSI_COLOR_BLUE << "[ RUN      ] " << ANSI_COLOR_RESET << #test_func << std::endl; \
        g_tests_run++; \
        try { \
            test_func(); \
            std::cout << ANSI_COLOR_GREEN << "[       OK ] " << ANSI_COLOR_RESET << #test_func << std::endl; \
        } catch (const std::exception& e) { \
            std::cerr << ANSI_COLOR_RED << "[  FAILED  ] " << ANSI_COLOR_RESET << #test_func << " (Exception: " << e.what() << ")" << std::endl; \
            g_tests_failed++; \
        } catch (...) { \
            std::cerr << ANSI_COLOR_RED << "[  FAILED  ] " << ANSI_COLOR_RESET << #test_func << " (Unknown Exception)" << std::endl; \
            g_tests_failed++; \
        } \
    } while (0)

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << ANSI_COLOR_RED << "  Assertion failed: " << #condition << " at " << __FILE__ << ":" << __LINE__ << ANSI_COLOR_RESET << std::endl; \
            throw std::runtime_error("Assertion failed: " #condition); \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2) \
    do { \
        if ((val1) != (val2)) { \
            std::cerr << ANSI_COLOR_RED << "  Assertion failed: " << #val1 << " == " << #val2 \
                      << " (Actual: " << (val1) << ", Expected: " << (val2) << ") at " \
                      << __FILE__ << ":" << __LINE__ << ANSI_COLOR_RESET << std::endl; \
            throw std::runtime_error("Assertion failed: " #val1 " == " #val2); \
        } \
    } while (0)

// ---------------------------------------------------------
// Test Cases
// ---------------------------------------------------------

void test_id_util() {
    unsigned int id1 = IdUtil::get_unique_id();
    unsigned int id2 = IdUtil::get_unique_id();
    ASSERT_TRUE(id1 > 0);
    ASSERT_EQ(id2, id1 + 1);
}

void test_string_util() {
    auto tokens = StringUtil::split("a,b,c", ",");
    ASSERT_EQ(tokens.size(), 3);
    ASSERT_EQ(tokens[0], "a");
    ASSERT_EQ(tokens[1], "b");
    ASSERT_EQ(tokens[2], "c");

    std::string replaced = StringUtil::replace("hello world", "world", "coopa");
    ASSERT_EQ(replaced, "hello coopa");

    std::vector<std::string> parts = {"x", "y", "z"};
    std::string joined = StringUtil::join(parts, "-");
    ASSERT_EQ(joined, "x-y-z");

    ASSERT_EQ(StringUtil::lower("CoOpA"), "coopa");
    ASSERT_EQ(StringUtil::upper("CoOpA"), "COOPA");
}

void test_math_util() {
    ASSERT_EQ(MathUtil::get_max(5.5f, 3.2f), 5.5f);
    ASSERT_EQ(MathUtil::get_min(5.5f, 3.2f), 3.2f);
    ASSERT_EQ(MathUtil::lerp(0.0f, 10.0f, 0.5f), 5.0f);

    Mat4 identity = Mat4::identity();
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (i == j) {
                ASSERT_EQ(identity.m[i][j], 1.0f);
            } else {
                ASSERT_EQ(identity.m[i][j], 0.0f);
            }
        }
    }

    Mat4 trans = Mat4::translation(2.0f, -3.0f, 4.5f);
    ASSERT_EQ(trans.m[3][0], 2.0f);
    ASSERT_EQ(trans.m[3][1], -3.0f);
    ASSERT_EQ(trans.m[3][2], 4.5f);

    Mat4 m1 = Mat4::translation(1.0f, 2.0f, 3.0f);
    Mat4 m2 = Mat4::translation(4.0f, 5.0f, 6.0f);
    Mat4 m3 = m1 * m2;
    ASSERT_EQ(m3.m[3][0], 5.0f);
    ASSERT_EQ(m3.m[3][1], 7.0f);
    ASSERT_EQ(m3.m[3][2], 9.0f);

    glm::mat4 glm_m = trans.get_mat();
    ASSERT_EQ(glm_m[0][3], 2.0f);
    ASSERT_EQ(glm_m[1][3], -3.0f);
    ASSERT_EQ(glm_m[2][3], 4.5f);

    Mat4 trans_back = Mat4::from_mat(glm_m);
    ASSERT_EQ(trans_back.m[3][0], 2.0f);
    ASSERT_EQ(trans_back.m[3][1], -3.0f);
    ASSERT_EQ(trans_back.m[3][2], 4.5f);
}

void test_file_util() {
    // Check path existence
    ASSERT_TRUE(FileUtil::does_path_exist(FileUtil::get_root_path("CMakeLists.txt")));
    auto [path, exist] = FileUtil::get_asset_path("non_existent_file_example");
    ASSERT_TRUE(!exist);
}

void test_yaml_map() {
    coopa::collections::YAMLMap map;
    map.set<int>("some_int", 42);
    map.set<std::string>("some_str", "hello");
    ASSERT_EQ(map.get<int>("some_int", 0), 42);
    ASSERT_EQ(map.get<std::string>("some_str", ""), "hello");

    std::string test_yaml = "test_temp_config.yaml";
    map.save(test_yaml);

    coopa::collections::YAMLMap map_loaded = coopa::collections::YAMLMap::load(test_yaml);
    ASSERT_EQ(map_loaded.get<int>("some_int", 0), 42);
    ASSERT_EQ(map_loaded.get<std::string>("some_str", ""), "hello");
    ASSERT_TRUE(map_loaded.exists("some_int"));
    ASSERT_TRUE(!map_loaded.exists("non_existent"));

    std::vector<int> vec = {1, 2, 3};
    map_loaded.set_vector<int>("numbers", vec);
    auto vec_loaded = map_loaded.get_vector<int>("numbers");
    ASSERT_EQ(vec_loaded.size(), 3);
    ASSERT_EQ(vec_loaded[0], 1);
    ASSERT_EQ(vec_loaded[1], 2);
    ASSERT_EQ(vec_loaded[2], 3);

    coopa::collections::YAMLMap nested;
    nested.set<double>("pi", 3.14159);
    map_loaded.set<fkyaml::node>("nested", nested.get_raw_node());
    auto nested_loaded = map_loaded.get_node("nested");
    ASSERT_EQ(nested_loaded.get<double>("pi", 0.0), 3.14159);

    std::remove(test_yaml.c_str());
}

void test_parallel_queue() {
    coopa::job::ParallelQueue<int> queue;
    ASSERT_TRUE(queue.empty());
    ASSERT_EQ(queue.size(), 0);

    queue.push(10);
    queue.push(20);
    ASSERT_EQ(queue.size(), 2);

    int val = 0;
    ASSERT_TRUE(queue.try_pop(val));
    ASSERT_EQ(val, 10);
    ASSERT_EQ(queue.size(), 1);

    std::vector<int> all;
    queue.pop_all(all);
    ASSERT_EQ(all.size(), 1);
    ASSERT_EQ(all[0], 20);
    ASSERT_TRUE(queue.empty());
}

void test_parallel_vector() {
    coopa::job::ParallelVector<std::string> pvec;
    ASSERT_TRUE(pvec.empty());

    pvec.push_back("a");
    pvec.push_back("b");
    pvec.push_back("a");
    ASSERT_EQ(pvec.size(), 3);

    std::string item;
    ASSERT_TRUE(pvec.try_pop_back(item));
    ASSERT_EQ(item, "a");

    ASSERT_TRUE(pvec.try_pop_front(item));
    ASSERT_EQ(item, "a");

    ASSERT_EQ(pvec.at(0), "b");

    pvec.push_back("c");
    std::vector<std::string> popped;
    pvec.pop_all(popped);
    ASSERT_EQ(popped.size(), 2);
    ASSERT_EQ(popped[0], "b");
    ASSERT_EQ(popped[1], "c");

    pvec.push_back("x");
    pvec.push_back("y");
    pvec.push_back("x");
    size_t removed = pvec.remove_all("x");
    ASSERT_EQ(removed, 2);
    ASSERT_EQ(pvec.size(), 1);
    ASSERT_EQ(pvec.at(0), "y");
}

void test_parallel_map() {
    coopa::job::ParallelMap<std::string, int> pmap;
    pmap.add("one", 1);
    pmap.add("two", 2);
    ASSERT_TRUE(pmap.contains("one"));
    ASSERT_EQ(pmap.size(), 2);

    auto opt = pmap.get("one");
    ASSERT_TRUE(opt.has_value());
    ASSERT_EQ(opt.value(), 1);

    auto snap = pmap.snapshot();
    ASSERT_EQ(snap.size(), 2);

    pmap.remove("one");
    ASSERT_TRUE(!pmap.contains("one"));
    ASSERT_EQ(pmap.size(), 1);
}

void test_job_engine() {
    coopa::job::JobEngine engine(4);
    engine.begin_frame();
    
    std::atomic<int> counter{0};
    coopa::job::JobHandle handle = engine.create_handle();
    
    engine.submit([&]() {
        counter.fetch_add(1);
    }, 1, handle);

    engine.submit([&]() {
        counter.fetch_add(2);
    }, 1, handle);

    engine.wait_for(handle);
    ASSERT_EQ(counter.load(), 3);

    engine.end_frame();
}

struct ComponentA {};
struct ComponentB {};

void test_job_scheduler() {
    coopa::job::JobEngine engine(4);
    coopa::job::JobScheduler scheduler(engine);

    scheduler.begin_frame();

    std::atomic<int> step{0};
    
    scheduler.add_job([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        step.store(1);
    }, 1, {}, {typeid(ComponentA)});

    scheduler.add_job([&]() {
        if (step.load() == 1) {
            step.store(2);
        }
    }, 1, {typeid(ComponentA)}, {});

    auto handles = scheduler.submit_all();
    scheduler.wait_for_all(handles);

    ASSERT_EQ(step.load(), 2);

    scheduler.end_frame();
}

void test_job_scheduler_main_thread() {
    coopa::job::JobEngine engine(4);
    coopa::job::JobScheduler scheduler(engine);

    scheduler.begin_frame();

    std::atomic<int> step{0};

    scheduler.add_job([&]() {
        step.store(42);
    }, 1, {}, {}, true /* is_main_thread_job */);

    auto handles = scheduler.submit_all();
    ASSERT_EQ(step.load(), 0);

    scheduler.execute_main_thread_jobs();
    ASSERT_EQ(step.load(), 42);

    scheduler.end_frame();
}

void test_job_engine_dependencies() {
    coopa::job::JobEngine engine(4);
    engine.begin_frame();

    std::atomic<int> value{0};

    // Job A: sets value to 10 after a short delay.
    coopa::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        value.store(10, std::memory_order_release);
    }, 1, handle_a);

    // Job B: depends on A, multiplies value by 3.
    // If dependency works, B runs after A and value becomes 30.
    // If dependency is broken, B may run before A and value could be 0*3=0 or 10.
    coopa::job::JobHandle handle_b = engine.create_handle();
    coopa::job::JobHandle deps_b[] = { handle_a };
    engine.submit([&]() {
        int v = value.load(std::memory_order_acquire);
        value.store(v * 3, std::memory_order_release);
    }, 1, handle_b, deps_b, 1);

    engine.wait_for(handle_b);
    ASSERT_EQ(value.load(), 30);

    engine.end_frame();
}

void test_job_engine_chained_dependencies() {
    coopa::job::JobEngine engine(4);
    engine.begin_frame();

    // Track execution order: A must run before B, B before C.
    std::atomic<int> sequence{0};
    std::atomic<bool> order_correct{true};

    // Job A: sets sequence to 1.
    coopa::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int expected = 0;
        if (!sequence.compare_exchange_strong(expected, 1)) {
            order_correct.store(false);
        }
    }, 1, handle_a);

    // Job B: depends on A, sets sequence to 2.
    coopa::job::JobHandle handle_b = engine.create_handle();
    coopa::job::JobHandle deps_b[] = { handle_a };
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        int expected = 1;
        if (!sequence.compare_exchange_strong(expected, 2)) {
            order_correct.store(false);
        }
    }, 1, handle_b, deps_b, 1);

    // Job C: depends on B (transitively on A), sets sequence to 3.
    coopa::job::JobHandle handle_c = engine.create_handle();
    coopa::job::JobHandle deps_c[] = { handle_b };
    engine.submit([&]() {
        int expected = 2;
        if (!sequence.compare_exchange_strong(expected, 3)) {
            order_correct.store(false);
        }
    }, 1, handle_c, deps_c, 1);

    engine.wait_for(handle_c);
    ASSERT_EQ(sequence.load(), 3);
    ASSERT_TRUE(order_correct.load());

    engine.end_frame();
}

void test_job_engine_fan_in_dependencies() {
    coopa::job::JobEngine engine(4);
    engine.begin_frame();

    // Two independent jobs (A and B) must both complete before C runs.
    std::atomic<int> completed_count{0};
    std::atomic<bool> c_ran_after_both{false};

    // Job A: increments completed_count after delay.
    coopa::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        completed_count.fetch_add(1, std::memory_order_release);
    }, 1, handle_a);

    // Job B: increments completed_count after different delay.
    coopa::job::JobHandle handle_b = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        completed_count.fetch_add(1, std::memory_order_release);
    }, 1, handle_b);

    // Job C: depends on BOTH A and B.
    coopa::job::JobHandle handle_c = engine.create_handle();
    coopa::job::JobHandle deps_c[] = { handle_a, handle_b };
    engine.submit([&]() {
        // Both A and B should have completed (count == 2) before C runs.
        c_ran_after_both.store(
            completed_count.load(std::memory_order_acquire) == 2
        );
    }, 1, handle_c, deps_c, 2);

    engine.wait_for(handle_c);
    ASSERT_EQ(completed_count.load(), 2);
    ASSERT_TRUE(c_ran_after_both.load());

    engine.end_frame();
}

void test_job_engine_repeated_engines_no_lost_or_phantom_jobs() {
    // Regression test for two bugs found while building coopa::asset (which
    // constructs a fresh, short-lived JobEngine per AssetManager and calls
    // wait_for() from the main thread immediately after submit()):
    //
    // 1. submit()'s no-dependency path used wake_one_worker() (notify_one())
    //    for a job deposited into one SPECIFIC thread's inbox. notify_one()
    //    can wake an unrelated idle thread instead of that inbox's owner; if
    //    every other worker was already asleep, the real owner never woke
    //    and wait_for() on that job's handle hung forever.
    // 2. WorkStealingDeque::pop() and steal() both used to move their output
    //    item out of the shared buffer slot BEFORE confirming their CAS on
    //    top_ actually won ownership of that slot. On the well-known
    //    contended "last element" path, the loser had already performed an
    //    unsynchronized move-read of the same slot the winner was also
    //    moving from. For a plain scalar this is harmless in practice; for
    //    Job (whose TaskWrapper move nulls out the source's callable) it can
    //    silently hand back a corrupted, no-op job while the counter still
    //    gets decremented as if it ran — wait_for() returns having believed
    //    the job completed, but its body never executed.
    //
    // A tight repeated construct/submit/wait_for loop reproduced both within
    // a few hundred iterations before the fix; this keeps a permanent,
    // bounded-runtime tripwire for a regression.
    const int k_iterations = 400;
    for (int i = 0; i < k_iterations; ++i) {
        coopa::job::JobEngine engine(2);
        std::atomic<bool> ran{false};
        coopa::job::JobHandle handle = engine.create_handle();
        engine.submit([&ran]() {
            ran.store(true, std::memory_order_release);
        }, 1, handle);
        engine.wait_for(handle);
        ASSERT_TRUE(ran.load(std::memory_order_acquire));
    }
}

void test_work_stealing_deque() {
    coopa::job::WorkStealingDeque<int> deque(16);

    // Empty checks.
    ASSERT_TRUE(deque.empty_approx());
    ASSERT_EQ(deque.size_approx(), 0);

    // Push and pop (owner operations).
    ASSERT_TRUE(deque.push(10));
    ASSERT_TRUE(deque.push(20));
    ASSERT_TRUE(deque.push(30));
    ASSERT_EQ(deque.size_approx(), 3);

    int val = 0;
    ASSERT_TRUE(deque.pop(val));
    ASSERT_EQ(val, 30); // LIFO from bottom.

    // Steal (thief operation — takes from top).
    ASSERT_TRUE(deque.steal(val));
    ASSERT_EQ(val, 10); // FIFO from top.

    // One item left.
    ASSERT_EQ(deque.size_approx(), 1);
    ASSERT_TRUE(deque.pop(val));
    ASSERT_EQ(val, 20);

    // Now empty.
    ASSERT_TRUE(deque.empty_approx());
    ASSERT_TRUE(!deque.pop(val));
    ASSERT_TRUE(!deque.steal(val));

    // Clear test.
    deque.push(99);
    deque.clear();
    ASSERT_TRUE(deque.empty_approx());
}

void test_work_stealing_deque_no_torn_moves_under_contention() {
    // Regression test: pop() and steal() both used to move their output item
    // out of buffer_[idx] BEFORE their CAS on top_ confirmed they actually
    // won that slot. On the contended "exactly one item left" path, the
    // loser (owner pop() vs. a thief, or two racing thieves) had already
    // done an unsynchronized move-read of the same slot the winner was also
    // moving from. A plain int can't reveal this (a racy read of a scalar
    // just yields a value, not corruption); a type whose move leaves the
    // source in a detectably-different state can.
    using coopa::job::WorkStealingDeque;

    struct NullingMovable {
        int  value = -1;
        bool alive = false;
        NullingMovable() = default;
        explicit NullingMovable(int v) : value(v), alive(true) {}
        NullingMovable(NullingMovable&& other) noexcept : value(other.value), alive(other.alive) {
            other.alive = false;
            other.value = -1;
        }
        NullingMovable& operator=(NullingMovable&& other) noexcept {
            value = other.value;
            alive = other.alive;
            other.alive = false;
            other.value = -1;
            return *this;
        }
        NullingMovable(const NullingMovable&) = delete;
        NullingMovable& operator=(const NullingMovable&) = delete;
    };

    const int k_trials = 3000;
    for (int trial = 0; trial < k_trials; ++trial) {
        WorkStealingDeque<NullingMovable> deque(64);
        deque.push(NullingMovable(42));

        std::atomic<int> success_count{0};
        std::atomic<int> phantom_count{0};

        auto thief = [&]() {
            NullingMovable val;
            if (deque.steal(val)) {
                success_count.fetch_add(1);
                if (!val.alive) phantom_count.fetch_add(1);
            }
        };
        auto owner_pop = [&]() {
            NullingMovable val;
            if (deque.pop(val)) {
                success_count.fetch_add(1);
                if (!val.alive) phantom_count.fetch_add(1);
            }
        };

        std::thread t1(thief);
        std::thread t2(owner_pop);
        t1.join();
        t2.join();

        ASSERT_EQ(success_count.load(), 1);
        ASSERT_EQ(phantom_count.load(), 0);
    }
}

// ---------------------------------------------------------
// Job system rework: generation-tagged handles, unbounded
// dependencies, parallel_for, cancellation, priority.
// ---------------------------------------------------------

void test_job_handle_generation_prevents_stale_aliasing() {
    coopa::job::JobEngine engine(2, 8, 4); // handle_pool_capacity = 4
    coopa::job::JobHandle first = engine.create_handle();
    ASSERT_TRUE(first.is_valid());
    engine.submit([]{}, 0, first);
    engine.wait_for(first);
    ASSERT_TRUE(first.is_complete());

    coopa::job::JobHandle stale_copy = first; // same slot + generation
    first.close(); // returns the slot to the free list

    std::vector<coopa::job::JobHandle> churn;
    for (int i = 0; i < 4; ++i) {
        auto h = engine.create_handle();
        ASSERT_TRUE(h.is_valid());
        churn.push_back(h);
    }

    // stale_copy's generation can no longer match whichever handle now
    // occupies that slot -- it must report complete regardless, and must
    // never be mistaken for one of the new occupants.
    ASSERT_TRUE(stale_copy.is_complete());
    for (auto& h : churn) {
        ASSERT_TRUE(!(h == stale_copy));
        h.close();
    }
}

void test_job_engine_fan_in_16_dependencies() {
    // The previous design silently truncated dependency lists to
    // k_max_inline_dependencies (4); this exercises 16 to prove that cliff
    // is gone.
    coopa::job::JobEngine engine(4);
    constexpr int kDeps = 16;
    std::atomic<int> completed{0};
    std::vector<coopa::job::JobHandle> deps;
    for (int i = 0; i < kDeps; ++i) {
        auto h = engine.create_handle();
        engine.submit([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            completed.fetch_add(1, std::memory_order_release);
        }, 0, h);
        deps.push_back(h);
    }

    coopa::job::JobHandle final_handle = engine.create_handle();
    bool all_done_before_final = false;
    engine.submit([&]() {
        all_done_before_final = (completed.load(std::memory_order_acquire) == kDeps);
    }, 0, final_handle, deps.data(), static_cast<uint32_t>(deps.size()));

    engine.wait_for(final_handle);
    ASSERT_EQ(completed.load(), kDeps);
    ASSERT_TRUE(all_done_before_final);

    for (auto& h : deps) h.close();
    final_handle.close();
}

void test_job_engine_deque_overflow_falls_back_to_global_queue() {
    // Small per-priority deque capacity so a single worker spawning many
    // child jobs onto its own deque is guaranteed to overflow it (200
    // children > 64 capacity). A full deque must fall back to the global
    // queue rather than lose the job.
    //
    // Deliberately not smaller than this: WorkStealingDeque's fixed-size
    // ring buffer (like other classic Chase-Lev implementations) assumes a
    // thief's steal() -- which reserves a slot via a CAS on top_ and only
    // *then* reads out of it -- completes before the owner wraps around and
    // reuses that same physical slot for a new push(). That assumption holds
    // overwhelmingly in real workloads (capacity is normally in the
    // thousands), but shrinking capacity far enough relative to how hard a
    // single test hammers push()/steal() concurrently (e.g. capacity 4 with
    // this same 200-iteration loop) can make the reuse race observable under
    // ThreadSanitizer. This is a property of the fixed-capacity Chase-Lev
    // design generally, not something introduced by any specific caller.
    coopa::job::JobEngine engine(1, 64);
    constexpr int kChildren = 200;
    std::atomic<int> ran{0};
    coopa::job::JobHandle handle = engine.create_handle();
    engine.submit([&engine, handle, &ran]() {
        for (int i = 0; i < kChildren; ++i) {
            engine.submit([&ran]() { ran.fetch_add(1, std::memory_order_relaxed); }, 0, handle);
        }
    }, 0, handle);

    engine.wait_for(handle);
    ASSERT_EQ(ran.load(), kChildren);
    handle.close();
}

void test_job_engine_counter_pool_exhaustion_recovers() {
    coopa::job::JobEngine engine(2, 64, 2); // handle_pool_capacity = 2
    coopa::job::JobHandle a = engine.create_handle();
    coopa::job::JobHandle b = engine.create_handle();
    ASSERT_TRUE(a.is_valid());
    ASSERT_TRUE(b.is_valid());

    coopa::job::JobHandle c = engine.create_handle();
    ASSERT_TRUE(!c.is_valid()); // pool exhausted

    engine.submit([]{}, 0, a);
    engine.wait_for(a);
    a.close(); // frees one slot

    coopa::job::JobHandle recovered = engine.create_handle();
    ASSERT_TRUE(recovered.is_valid()); // pool recovered after a close(), no frame boundary needed

    engine.submit([]{}, 0, b);
    engine.wait_for(b);
    b.close();
    engine.submit([]{}, 0, recovered);
    engine.wait_for(recovered);
    recovered.close();
}

void test_job_engine_nested_wait_for_from_worker() {
    // Single worker: `inner` can only ever be picked up by this same worker
    // acting as a real worker during its nested wait_for(), not by another
    // worker stealing it -- a genuine regression tripwire for the guest-only
    // wait_for() that used to be unable to touch its own deque.
    coopa::job::JobEngine engine(1);
    std::atomic<bool> inner_ran{false};

    coopa::job::JobHandle outer = engine.create_handle();
    engine.submit([&engine, &inner_ran]() {
        coopa::job::JobHandle inner = engine.create_handle();
        engine.submit([&inner_ran]() { inner_ran.store(true, std::memory_order_release); }, 0, inner);
        engine.wait_for(inner); // must not deadlock
        inner.close();
    }, 0, outer);

    engine.wait_for(outer);
    ASSERT_TRUE(inner_ran.load());
    outer.close();
}

void test_job_engine_parallel_for_covers_range_exactly_once() {
    coopa::job::JobEngine engine(4);
    auto check = [&engine](size_t count, size_t grain) {
        std::vector<std::atomic<int>> hits(count > 0 ? count : 1);
        for (auto& h : hits) h.store(0);
        engine.parallel_for_blocking(count, grain, [&hits](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) hits[i].fetch_add(1, std::memory_order_relaxed);
        });
        for (size_t i = 0; i < count; ++i) {
            ASSERT_EQ(hits[i].load(), 1);
        }
    };
    check(100, 1);
    check(100, 100); // one chunk
    check(100, 0);   // auto grain
    check(0, 4);     // empty range -- no chunks, no crash
    check(3, 10);    // count < grain -- exactly one chunk covering [0,3)
}

void test_job_engine_cancel_skips_body_but_releases_counter() {
    coopa::job::JobEngine engine(2);
    std::atomic<bool> gate_open{false};
    std::atomic<bool> ran{false};

    coopa::job::JobHandle gate = engine.create_handle();
    engine.submit([&gate_open]() {
        while (!gate_open.load(std::memory_order_acquire)) std::this_thread::yield();
    }, 0, gate);

    coopa::job::JobHandle handle = engine.create_handle();
    coopa::job::JobHandle deps[] = { gate };
    engine.submit([&ran]() { ran.store(true); }, 0, handle, deps, 1);

    engine.cancel(handle); // cancel while still parked on `gate`
    gate_open.store(true, std::memory_order_release);

    engine.wait_for(handle);
    ASSERT_TRUE(!ran.load());
    gate.close();
    handle.close();
}

void test_job_engine_priority_ordering_single_worker() {
    coopa::job::JobEngine engine(1);
    std::atomic<bool> gate_open{false};
    coopa::job::JobHandle gate = engine.create_handle();
    engine.submit([&gate_open]() {
        while (!gate_open.load(std::memory_order_acquire)) std::this_thread::yield();
    }, 0, gate);

    std::vector<int> order;
    std::mutex order_mutex;
    coopa::job::JobHandle low = engine.create_handle();
    coopa::job::JobHandle high = engine.create_handle();
    engine.submit([&]() { std::lock_guard<std::mutex> l(order_mutex); order.push_back(0); },
                  0, low, nullptr, 0u, coopa::job::Priority::Low);
    engine.submit([&]() { std::lock_guard<std::mutex> l(order_mutex); order.push_back(1); },
                  0, high, nullptr, 0u, coopa::job::Priority::High);

    gate_open.store(true, std::memory_order_release);
    engine.wait_for(low);
    engine.wait_for(high);

    ASSERT_EQ(order.size(), static_cast<size_t>(2));
    ASSERT_EQ(order[0], 1); // High ran before Low
    ASSERT_EQ(order[1], 0);

    gate.close();
    low.close();
    high.close();
}

void test_job_handle_survives_begin_end_frame() {
    coopa::job::JobEngine engine(2);
    coopa::job::JobHandle handle = engine.create_handle();
    engine.begin_frame();
    engine.end_frame();
    engine.begin_frame(); // a second begin_frame -- must NOT invalidate `handle`

    std::atomic<bool> ran{false};
    engine.submit([&ran]() { ran.store(true); }, 0, handle);
    engine.wait_for(handle);
    ASSERT_TRUE(ran.load());
    engine.end_frame();
    handle.close();
}

void test_job_engine_shared_across_frames_and_subsystems() {
    // Simulates two independent subsystems sharing one engine across several
    // frames with no coordination beyond the engine itself -- the pattern
    // that used to require AssetManager to own a private JobEngine.
    coopa::job::JobEngine engine(2);
    std::atomic<int> subsystem_a_count{0};
    std::atomic<int> subsystem_b_count{0};
    for (int frame = 0; frame < 5; ++frame) {
        engine.begin_frame();
        coopa::job::JobHandle a = engine.create_handle();
        coopa::job::JobHandle b = engine.create_handle();
        engine.submit([&]() { subsystem_a_count.fetch_add(1); }, 0, a);
        engine.submit([&]() { subsystem_b_count.fetch_add(1); }, 0, b);
        engine.wait_for(a);
        engine.wait_for(b);
        a.close();
        b.close();
        engine.end_frame();
    }
    ASSERT_EQ(subsystem_a_count.load(), 5);
    ASSERT_EQ(subsystem_b_count.load(), 5);
}

void test_debug_logging() {
    coopa::debug::Logger logger("TestLogger");
    logger.info("This is an info log");
    logger.warn("This is a warning log");
    logger.error("This is an error log");

    coopa::debug::DebugManager manager;
    auto& ctx = manager.get_context();
    ctx.info("Message from context 1", "TAG_1");
    ctx.info("Message from context 2", "TAG_2");
    manager.show();

    coopa::debug::DebugBucket bucket;
    auto& bctx = bucket.get_context();
    bctx.info("Bucket message 1", "BUCKET_TAG");
    bucket.show();
}

// ---------------------------------------------------------
// coopa::event::Signal
// ---------------------------------------------------------

void test_signal_basic_emit() {
    using coopa::event::Signal;
    using coopa::event::k_invalid_slot;

    Signal<int> s;
    int a = 0, b = 0;
    s.connect([&](int v) { a += v; });
    s.connect([&](int v) { b += v; });
    ASSERT_TRUE(s.slot_count() == 2);

    s.emit(5);
    ASSERT_TRUE(a == 5 && b == 5);
    s.emit(5);
    ASSERT_TRUE(a == 10 && b == 10);

    Signal<> empty_signal;
    ASSERT_TRUE(empty_signal.slot_count() == 0);
    empty_signal.emit();  // no-op, must not throw/crash

    auto c = empty_signal.connect({});  // empty std::function is ignored
    ASSERT_TRUE(c.id() == k_invalid_slot);
    ASSERT_TRUE(empty_signal.slot_count() == 0);
}

void test_signal_disconnect() {
    using coopa::event::Signal;

    Signal<int> s;
    int a = 0, b = 0, c = 0;
    s.connect([&](int v) { a += v; });
    auto cb = s.connect([&](int v) { b += v; });
    s.connect([&](int v) { c += v; });

    ASSERT_TRUE(cb.disconnect());
    ASSERT_TRUE(s.slot_count() == 2);

    s.emit(1);
    ASSERT_TRUE(a == 1 && b == 0 && c == 1);  // order preserved after removing the middle slot

    ASSERT_TRUE(!cb.connected());
    ASSERT_TRUE(!cb.disconnect());       // already gone
    ASSERT_TRUE(!s.disconnect(99999));   // unknown id
}

void test_signal_scoped_connection() {
    using coopa::event::Signal;
    using coopa::event::ScopedConnection;

    Signal<> s;
    int count = 0;
    {
        ScopedConnection sc = s.connect_scoped([&] { ++count; });
        ASSERT_TRUE(s.slot_count() == 1);
        s.emit();
    }
    ASSERT_TRUE(s.slot_count() == 0);  // scope exit disconnected it
    s.emit();
    ASSERT_TRUE(count == 1);

    ScopedConnection outer;
    {
        ScopedConnection inner = s.connect_scoped([&] { ++count; });
        outer = std::move(inner);  // move-construct/assign out of the inner scope
    }
    ASSERT_TRUE(s.slot_count() == 1);
    s.emit();
    ASSERT_TRUE(count == 2);

    ScopedConnection other = s.connect_scoped([&] { ++count; });
    ASSERT_TRUE(s.slot_count() == 2);
    outer = std::move(other);  // move-assign over a live connection disconnects the old one
    ASSERT_TRUE(s.slot_count() == 1);

    auto released = outer.release();  // gives up ownership without disconnecting
    ASSERT_TRUE(released.connected());
}

void test_signal_reentrancy_self_disconnect() {
    using coopa::event::Signal;
    using coopa::event::Connection;

    Signal<> s;
    int calls = 0;
    int after_disconnect_marker = 0;
    Connection self;
    self = s.connect([&] {
        ++calls;
        self.disconnect();
        // If Signal destroyed the running std::function on disconnect() (rather than
        // tombstoning it), this write would be use-after-free.
        after_disconnect_marker = 42;
    });

    s.emit();
    ASSERT_TRUE(calls == 1);
    ASSERT_TRUE(after_disconnect_marker == 42);
    s.emit();
    ASSERT_TRUE(calls == 1);  // it really disconnected
}

void test_signal_reentrancy_connect_during_emit() {
    using coopa::event::Signal;
    using coopa::event::Connection;
    using coopa::event::k_invalid_slot;

    Signal<> s;
    int a_calls = 0, b_calls = 0, c_calls = 0;
    Connection c_conn;
    s.connect([&] {
        ++a_calls;
        s.connect([&] { ++b_calls; });  // connecting mid-emit must not run this emit
        if (c_conn.id() != k_invalid_slot) c_conn.disconnect();
    });
    c_conn = s.connect([&] { ++c_calls; });

    s.emit();
    ASSERT_TRUE(a_calls == 1);
    ASSERT_TRUE(b_calls == 0);  // connected during this emit: runs next time, not now
    ASSERT_TRUE(c_calls == 0);  // disconnected earlier in this same emit: skipped
    ASSERT_TRUE(s.slot_count() == 2);  // a + b; c was removed

    s.emit();
    ASSERT_TRUE(b_calls == 1);
}

void test_signal_disconnect_all_and_id_reuse() {
    using coopa::event::Signal;

    Signal<> s;
    auto c1 = s.connect([] {});
    auto c2 = s.connect([] {});
    auto c3 = s.connect([] {});

    s.disconnect_all();
    ASSERT_TRUE(s.empty());
    s.emit();  // no-op
    ASSERT_TRUE(!c1.connected() && !c2.connected() && !c3.connected());

    int fired = 0;
    s.connect([&] { ++fired; });
    s.emit();
    ASSERT_TRUE(fired == 1);
    ASSERT_TRUE(!c1.connected());  // ids are never reused, so the old token stays dead
}

void test_signal_connection_outlives_signal() {
    using coopa::event::Signal;
    using coopa::event::ScopedConnection;

    auto sig = std::make_unique<Signal<>>();
    auto conn = sig->connect([] {});
    ScopedConnection sc = sig->connect_scoped([] {});

    sig.reset();

    ASSERT_TRUE(!conn.connected());
    ASSERT_TRUE(!conn.disconnect());
    // sc's destructor runs at scope exit and must not crash on the dead signal.
}

void test_signal_destroyed_from_slot() {
    using coopa::event::Signal;

    auto sig = std::make_unique<Signal<>>();
    bool ran = false;
    sig->connect([&] {
        ran = true;
        sig.reset();  // destroys the Signal while its own emit() is on the stack
    });

    sig->emit();  // must unwind cleanly rather than touching *sig afterward
    ASSERT_TRUE(ran);
}

void test_signal_reference_args_no_copy() {
    using coopa::event::Signal;

    struct Payload {
        int copies = 0;
        Payload() = default;
        Payload(const Payload& other) : copies(other.copies + 1) {}
    };

    Signal<const Payload&> s;
    int seen_copies = 0;
    s.connect([&](const Payload& p) { seen_copies += p.copies; });
    s.connect([&](const Payload& p) { seen_copies += p.copies; });

    Payload p;
    s.emit(p);
    ASSERT_TRUE(seen_copies == 0);  // emit() forwards the reference, never copies the argument
}

// ---------------------------------------------------------
// coopa::asset tests
// ---------------------------------------------------------

namespace asset_test {

/** @brief Trivial test asset: an in-memory string, decoded from a file's raw bytes. */
struct TextAsset {
    std::string contents;
};

/** @brief Loader for TextAsset: decode() reads bytes off-thread, finalize() just wraps them (no GPU step). */
class TextAssetLoader : public coopa::asset::TypedAssetLoader<TextAsset, std::vector<std::byte>> {
public:
    std::shared_ptr<std::vector<std::byte>> decode_typed(const coopa::asset::AssetId&, const coopa::asset::LoadContext& ctx) override {
        return std::make_shared<std::vector<std::byte>>(coopa::asset::AssetSource::read_bytes(ctx.resolved_path));
    }
    std::shared_ptr<TextAsset> finalize_typed(std::shared_ptr<std::vector<std::byte>> decoded, const coopa::asset::AssetId&, const coopa::asset::LoadContext&) override {
        auto asset = std::make_shared<TextAsset>();
        asset->contents.assign(reinterpret_cast<const char*>(decoded->data()), decoded->size());
        return asset;
    }
    const char* type_name() const override { return "TextAsset"; }
};

/** @brief Test asset that reports its own destruction through a flag the test still owns. */
struct TrackedAsset {
    std::shared_ptr<bool> destroyed;
    int                   tag = 0;
    ~TrackedAsset() { if (destroyed) *destroyed = true; }
};

} // namespace asset_test

void test_asset_id() {
    using coopa::asset::AssetId;

    AssetId a = AssetId::from_path("widgets/foo.txt");
    AssetId b = AssetId::from_path("./widgets/foo.txt");
    AssetId c = AssetId::from_path("widgets\\foo.txt");
    AssetId d = AssetId::from_path("widgets/bar.txt");

    ASSERT_TRUE(a.is_valid());
    ASSERT_TRUE(a == b);
    ASSERT_TRUE(a == c);
    ASSERT_TRUE(a.hash() == b.hash());
    ASSERT_TRUE(a != d);
    ASSERT_EQ(a.path(), std::string("widgets/foo.txt"));
    ASSERT_TRUE(!AssetId().is_valid());
}

void test_asset_source() {
    using coopa::asset::AssetSource;

    std::string test_file = "test_temp_asset_source.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "source contents";
    }

    AssetSource source;
    source.add_search_root(".");
    ASSERT_TRUE(source.exists(test_file));
    ASSERT_TRUE(!source.exists("definitely_missing_asset.txt"));

    std::string resolved = source.resolve(test_file);
    auto bytes = AssetSource::read_bytes(resolved);
    std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    ASSERT_EQ(contents, "source contents");

    // Not asserted > 0: this is an opaque ordering tick, not a real
    // timestamp (see AssetSource::last_write_time_ns's doc) -- only that it
    // isn't the "file not queryable" sentinel.
    ASSERT_TRUE(AssetSource::last_write_time_ns(resolved) != 0);

    std::remove(test_file.c_str());
}

void test_asset_manager_sync_load_and_cache() {
    using namespace asset_test;
    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());

    std::string test_file = "test_temp_asset_sync.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "sync contents";
    }

    auto h1 = assets.load<TextAsset>(test_file);
    ASSERT_TRUE(h1.is_loaded());
    ASSERT_EQ(h1->contents, "sync contents");
    ASSERT_EQ(h1.revision(), 1u);

    // Second load of the same path must hit the cached slot, not re-decode.
    auto h2 = assets.load<TextAsset>(test_file);
    ASSERT_TRUE(h2.get() == h1.get());
    ASSERT_EQ(h2.revision(), h1.revision());

    // A type with no registered loader must fail cleanly, never throw out of
    // load() -- use a distinct path so this hits the "no loader" branch
    // rather than the type-mismatch guard exercised below.
    struct Unregistered {};
    std::string other_file = "test_temp_asset_sync_unregistered.txt";
    { std::ofstream ofs(other_file); ofs << "x"; }
    auto h3 = assets.load<Unregistered>(other_file);
    ASSERT_TRUE(h3.is_failed());
    ASSERT_TRUE(!h3.error().empty());
    std::remove(other_file.c_str());

    // No explicit assets.shutdown() here: h1/h2/h3 are still live handles
    // into assets' slots, and shutdown() destroys every AssetSlot (see its
    // doc) -- dereferencing a handle afterward, including implicitly via its
    // destructor, is a use-after-free. AssetManager's own destructor calls
    // shutdown() for us, at function-exit time, which is safe here because
    // `assets` is declared before h1/h2/h3 and so (per C++'s reverse local
    // destruction order) is destroyed only after they already are.
    std::remove(test_file.c_str());
}

void test_asset_manager_rejects_type_mismatch() {
    // AssetId is purely path-based -- loading the same path as two
    // different C++ types must never let the second load reinterpret the
    // first type's payload. It must fail loudly instead.
    using namespace asset_test;
    struct OtherAsset { int x = 0; };

    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());

    std::string test_file = "test_temp_asset_type_mismatch.txt";
    { std::ofstream ofs(test_file); ofs << "shared path"; }

    auto text_handle = assets.load<TextAsset>(test_file);
    ASSERT_TRUE(text_handle.is_loaded());

    // No loader is even registered for OtherAsset -- if the type-mismatch
    // guard were missing, this would still "succeed" by handing back a
    // handle whose get() reinterprets TextAsset's payload as OtherAsset.
    auto other_handle = assets.load<OtherAsset>(test_file);
    ASSERT_TRUE(!other_handle.is_valid());
    ASSERT_TRUE(!other_handle.is_loaded());

    // The original handle must be completely unaffected.
    ASSERT_TRUE(text_handle.is_loaded());
    ASSERT_EQ(text_handle->contents, "shared path");

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; text_handle is still live here.
    std::remove(test_file.c_str());
}

void test_asset_manager_base_dir_prevents_collision() {
    // Regression test: identity used to be built from the raw virtual_path
    // alone, before it was resolved against base_dir. Two different scene
    // directories that both reference the same relative filename (e.g.
    // "meshes/cube.000.yaml" in gfxcoopa's real usage) would silently share
    // one cached slot and one would serve the other's asset. AssetId must
    // be built from the RESOLVED path instead, so distinct files with the
    // same relative name never collide.
    using namespace asset_test;
    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());

    std::string dir_a = "test_temp_asset_dir_a";
    std::string dir_b = "test_temp_asset_dir_b";
    std::filesystem::create_directory(dir_a);
    std::filesystem::create_directory(dir_b);
    { std::ofstream ofs(dir_a + "/shared.txt"); ofs << "from A"; }
    { std::ofstream ofs(dir_b + "/shared.txt"); ofs << "from B"; }

    auto handle_a = assets.load<TextAsset>("shared.txt", dir_a);
    auto handle_b = assets.load<TextAsset>("shared.txt", dir_b);

    ASSERT_TRUE(handle_a.is_loaded());
    ASSERT_TRUE(handle_b.is_loaded());
    ASSERT_TRUE(handle_a.get() != handle_b.get());  // distinct slots, not aliased
    ASSERT_EQ(handle_a->contents, "from A");
    ASSERT_EQ(handle_b->contents, "from B");

    // get() with the matching base_dir must find each one back.
    ASSERT_TRUE(assets.get<TextAsset>("shared.txt", dir_a).get() == handle_a.get());
    ASSERT_TRUE(assets.get<TextAsset>("shared.txt", dir_b).get() == handle_b.get());

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; handle_a/handle_b are still live here.
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
}

void test_asset_manager_async_load() {
    using namespace asset_test;
    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());

    std::string test_file = "test_temp_asset_async.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "async contents";
    }

    auto handle = assets.load_async<TextAsset>(test_file);
    ASSERT_TRUE(handle.state() == coopa::asset::AssetState::Loading || handle.is_loaded());

    int guard = 0;
    while (!handle.is_loaded() && !handle.is_failed() && guard++ < 1000) {
        assets.update(0.001f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(handle.is_loaded());
    ASSERT_EQ(handle->contents, "async contents");

    // A second load_async() for the same id while the first is already
    // resolved must reuse the slot rather than kicking off another job.
    auto handle2 = assets.load_async<TextAsset>(test_file);
    ASSERT_TRUE(handle2.is_loaded());
    ASSERT_TRUE(handle2.get() == handle.get());

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; handle/handle2 are still live here.
    std::remove(test_file.c_str());
}

void test_asset_manager_hot_reload() {
    using namespace asset_test;
    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());
    assets.set_hot_reload(true);
    assets.set_poll_interval(0.0f);

    std::string test_file = "test_temp_asset_reload.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "original";
    }

    auto handle = assets.load<TextAsset>(test_file);
    ASSERT_TRUE(handle.is_loaded());
    ASSERT_EQ(handle.revision(), 1u);

    bool reload_fired = false;
    coopa::asset::AssetId reloaded_id;
    coopa::event::ScopedConnection conn = assets.on_reloaded.connect_scoped(
        [&](const coopa::asset::AssetId& id) {
            reload_fired = true;
            reloaded_id = id;
        });

    // Give the filesystem clock room to register a distinct mtime.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::ofstream ofs(test_file);
        ofs << "reloaded contents";
    }

    assets.update(0.0f);

    ASSERT_TRUE(reload_fired);
    ASSERT_TRUE(reloaded_id == handle.id());
    ASSERT_EQ(handle.revision(), 2u);
    ASSERT_EQ(handle->contents, "reloaded contents");  // same handle, new payload

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; handle is still live here (conn is destroyed before assets
    // regardless, per reverse local-destruction order, so it never dangles).
    std::remove(test_file.c_str());
}

void test_asset_manager_unload_and_gc() {
    using namespace asset_test;
    coopa::asset::AssetManager assets;
    assets.register_loader<TextAsset>(std::make_unique<TextAssetLoader>());

    std::string test_file = "test_temp_asset_unload.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "unload me";
    }

    coopa::asset::AssetId id = coopa::asset::AssetId::from_path(test_file);
    auto handle = assets.load<TextAsset>(test_file);
    ASSERT_TRUE(handle.is_loaded());

    // unload() is a no-op while a handle still references the slot.
    assets.unload(id);
    ASSERT_TRUE(assets.get<TextAsset>(test_file).is_valid());

    handle = coopa::asset::AssetHandle<TextAsset>();  // drop the last handle
    assets.garbage_collect();
    ASSERT_TRUE(!assets.get<TextAsset>(test_file).is_valid());

    assets.shutdown();
    std::remove(test_file.c_str());
}

void test_asset_manager_shutdown_drains_pending() {
    using namespace asset_test;

    // A loader that reports whether finalize() actually ran, via a flag
    // outside the AssetManager -- the handle itself must not be dereferenced
    // after shutdown() (it clears slots_, exactly like every other
    // manual-teardown contract in this workspace: SceneLoader::
    // clear_component_parsers(), UIResourceCache::clear()), so this is the
    // only safe way to observe that the drain happened.
    struct TrackingLoader : public coopa::asset::TypedAssetLoader<TextAsset, std::vector<std::byte>> {
        std::shared_ptr<bool> finalized;
        std::shared_ptr<std::vector<std::byte>> decode_typed(const coopa::asset::AssetId&, const coopa::asset::LoadContext& ctx) override {
            return std::make_shared<std::vector<std::byte>>(coopa::asset::AssetSource::read_bytes(ctx.resolved_path));
        }
        std::shared_ptr<TextAsset> finalize_typed(std::shared_ptr<std::vector<std::byte>>, const coopa::asset::AssetId&, const coopa::asset::LoadContext&) override {
            *finalized = true;
            return std::make_shared<TextAsset>();
        }
        const char* type_name() const override { return "TextAsset"; }
    };

    auto finalized = std::make_shared<bool>(false);
    coopa::asset::AssetManager assets;
    auto loader = std::make_unique<TrackingLoader>();
    loader->finalized = finalized;
    assets.register_loader<TextAsset>(std::move(loader));

    std::string test_file = "test_temp_asset_shutdown.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "in flight";
    }

    {
        // Scoped so `handle` is destroyed (a harmless ref_count decrement on
        // a still-live slot) BEFORE shutdown() runs below -- shutdown()
        // destroys every AssetSlot (see its doc), and AssetHandle::
        // release_() (run by both its destructor and its assignment
        // operators) dereferences the slot unconditionally, so there is no
        // safe way to touch this handle again, including by reassigning it,
        // once shutdown() has executed.
        auto handle = assets.load_async<TextAsset>(test_file);
        ASSERT_TRUE(handle.is_valid());
    }

    // Deliberately do not call update() first -- shutdown() itself must
    // wait for the in-flight decode() job and run finalize() before tearing
    // down pending_, rather than destroying that job's state out from under
    // a still-running worker thread.
    assets.shutdown();

    ASSERT_TRUE(*finalized);

    std::remove(test_file.c_str());
}

void test_asset_manager_create_publishes_runtime_payload() {
    using namespace asset_test;

    // No loader registered for TrackedAsset at all -- create() must never
    // touch loaders_.
    coopa::asset::AssetManager assets;
    assets.set_hot_reload(true);
    assets.set_poll_interval(0.0f);

    auto destroyed = std::make_shared<bool>(false);
    auto payload = std::make_shared<TrackedAsset>();
    payload->destroyed = destroyed;
    payload->tag = 42;

    auto handle = assets.create<TrackedAsset>("runtime/generated", payload);
    ASSERT_TRUE(handle.is_loaded());
    ASSERT_EQ(handle.revision(), 1u);
    ASSERT_EQ(handle->tag, 42);
    ASSERT_TRUE(assets.get<TrackedAsset>("runtime/generated").get() == handle.get());

    // resolved_path is empty for a create()d slot, so poll_for_reloads_()
    // must skip it entirely -- revision must never move on its own.
    for (int i = 0; i < 5; ++i) {
        assets.update(0.0f);
    }
    ASSERT_EQ(handle.revision(), 1u);

    // Type mismatch: create()-ing the same id as a different type must fail
    // loudly and leave the existing slot completely alone.
    auto mismatched = assets.create<TextAsset>("runtime/generated", std::make_shared<TextAsset>());
    ASSERT_TRUE(!mismatched.is_valid());
    ASSERT_EQ(handle.revision(), 1u);
    ASSERT_TRUE(handle.is_loaded());

    payload.reset();
    handle = coopa::asset::AssetHandle<TrackedAsset>();
    assets.garbage_collect();
    ASSERT_TRUE(!assets.get<TrackedAsset>("runtime/generated").is_valid());
    // garbage_collect() retires the payload with the same grace period as
    // every other eviction path now (see retire_payload_()) -- it is not
    // destroyed synchronously just because the slot is gone.
    ASSERT_TRUE(!*destroyed);
    for (int i = 0; i < 3; ++i) {
        assets.update(0.0f);
    }
    ASSERT_TRUE(*destroyed);

    assets.shutdown();
}

void test_asset_manager_create_republishes_with_grace_period() {
    using namespace asset_test;

    coopa::asset::AssetManager assets;

    auto a_gone = std::make_shared<bool>(false);
    auto payload_a = std::make_shared<TrackedAsset>();
    payload_a->destroyed = a_gone;
    payload_a->tag = 1;

    auto handle = assets.create<TrackedAsset>("runtime/replaceable", payload_a);
    ASSERT_TRUE(handle.is_loaded());
    ASSERT_EQ(handle.revision(), 1u);

    bool reload_fired = false;
    coopa::asset::AssetId reloaded_id;
    coopa::event::ScopedConnection conn = assets.on_reloaded.connect_scoped(
        [&](const coopa::asset::AssetId& id) {
            reload_fired = true;
            reloaded_id = id;
        });

    // Drop the test's own reference to A -- only the slot's payload (plus,
    // shortly, the retire list) should be keeping it alive.
    payload_a.reset();

    auto payload_b = std::make_shared<TrackedAsset>();
    payload_b->tag = 2;
    assets.create<TrackedAsset>("runtime/replaceable", payload_b);
    payload_b.reset();

    ASSERT_TRUE(reload_fired);
    ASSERT_TRUE(reloaded_id == handle.id());
    ASSERT_EQ(handle.revision(), 2u);
    ASSERT_EQ(handle->tag, 2);           // same handle, new payload (address-stable slot)
    ASSERT_TRUE(!*a_gone);               // still within the grace period

    for (int i = 0; i < 3; ++i) {
        assets.update(0.0f);
    }
    ASSERT_TRUE(*a_gone);

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; handle is still live here.
}

void test_asset_manager_idle_eviction_defers_payload_destruction() {
    using namespace asset_test;

    coopa::asset::AssetManager assets;
    assets.set_idle_eviction(true);
    assets.set_max_idle_frames(2);
    ASSERT_TRUE(assets.idle_eviction_enabled());
    ASSERT_EQ(assets.max_idle_frames(), 2u);

    auto destroyed = std::make_shared<bool>(false);

    // Hold a handle for the first stretch, so we can prove a referenced
    // slot never ages regardless of how long update() keeps running.
    auto tracked = std::make_shared<TrackedAsset>();
    tracked->destroyed = destroyed;
    auto handle = assets.create<TrackedAsset>("runtime/evictable", tracked);
    tracked.reset();
    ASSERT_TRUE(handle.is_loaded());

    for (int i = 0; i < 10; ++i) {
        assets.update(0.0f);
    }
    ASSERT_TRUE(assets.get<TrackedAsset>("runtime/evictable").is_valid());
    ASSERT_TRUE(!*destroyed);

    // Drop the last handle -- the slot is now idle and should age out within
    // max_idle_frames() updates, but the payload must survive the grace
    // period after that.
    handle = coopa::asset::AssetHandle<TrackedAsset>();

    int updates_to_evict = 0;
    while (assets.get<TrackedAsset>("runtime/evictable").is_valid() && updates_to_evict < 10) {
        assets.update(0.0f);
        ++updates_to_evict;
    }
    ASSERT_TRUE(!assets.get<TrackedAsset>("runtime/evictable").is_valid());
    ASSERT_TRUE(updates_to_evict <= assets.max_idle_frames());
    ASSERT_TRUE(!*destroyed);  // evicted from the slot, but still in the grace-period retire list

    for (int i = 0; i < 3; ++i) {
        assets.update(0.0f);
    }
    ASSERT_TRUE(*destroyed);

    assets.shutdown();
}

// ---------------------------------------------------------
// Scene inheritance (inherit_from) tests
// ---------------------------------------------------------

namespace scene_inherit_test {

// A minimal test-only component so merges can be verified against real
// attached component instances rather than by re-inspecting YAML (which
// SceneLoader never exposes back out -- the pipeline is load-only).
class TestTagComponent : public coopa::scene::Component {
public:
    std::string type_name() const override { return "TestTag"; }
    std::string id;
    std::string label;
    std::string extra;
};

// A second test-only component whose sole job is to prove ctx.resolve()
// finds a relative path against the file that actually declared it -- the
// provenance a prefab merged in from another directory needs to keep working.
class TestAssetComponent : public coopa::scene::Component {
public:
    std::string type_name() const override { return "TestAsset"; }
    std::string resolved_path;
};

void register_components() {
    coopa::scene::SceneLoader::register_component_parser(
        "TestTag",
        [](const fkyaml::node& node, coopa::scene::SceneObject& obj, const coopa::scene::SceneLoader::ParseContext&) {
            auto* c = obj.add_component<TestTagComponent>();
            if (node.contains("id"))    c->id    = node.at("id").get_value<std::string>();
            if (node.contains("label")) c->label = node.at("label").get_value<std::string>();
            if (node.contains("extra")) c->extra = node.at("extra").get_value<std::string>();
        });
    coopa::scene::SceneLoader::register_component_parser(
        "TestAsset",
        [](const fkyaml::node& node, coopa::scene::SceneObject& obj, const coopa::scene::SceneLoader::ParseContext& ctx) {
            auto* c = obj.add_component<TestAssetComponent>();
            if (node.contains("file")) {
                c->resolved_path = ctx.resolve(node.at("file").get_value<std::string>());
            }
        });
}

std::vector<TestTagComponent*> find_tags(const coopa::scene::SceneObject& obj) {
    std::vector<TestTagComponent*> result;
    for (const auto& c : obj.components()) {
        if (auto* t = dynamic_cast<TestTagComponent*>(c.get())) result.push_back(t);
    }
    return result;
}

TestTagComponent* find_tag_by_id(const coopa::scene::SceneObject& obj, const std::string& id) {
    for (auto* t : find_tags(obj)) {
        if (t->id == id) return t;
    }
    return nullptr;
}

// Fixtures live under a fresh per-test subdirectory of the system temp dir so
// relative inherit_from paths (and relative asset paths inside inherited
// files) behave exactly like real files on disk, including across
// directories -- string-substitution fixtures can't exercise that.
std::string write_file(const std::string& test_name, const std::string& relative_path, const std::string& content) {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "libcoopa_scene_inherit_tests" / test_name / relative_path;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
    out.close();
    return path.string();
}

} // namespace scene_inherit_test

void test_scene_inherit_object_from_file() {
    using namespace scene_inherit_test;
    write_file("object_from_file", "prefab.yaml",
        "object:\n"
        "  name: Base\n"
        "  components:\n"
        "    - type: Transform\n"
        "      position: { x: 1.0, y: 2.0, z: 3.0 }\n"
        "    - type: TestTag\n"
        "      label: base-label\n"
        "  children:\n"
        "    - name: Child1\n"
        "      components:\n"
        "        - type: TestTag\n"
        "          label: child-label\n");
    std::string scene_path = write_file("object_from_file", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: prefab.yaml\n"
        "      components:\n"
        "        - type: TestTag\n"
        "          label: override-label\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    coopa::scene::SceneObject* obj = scene.find_object("Derived");
    ASSERT_TRUE(obj != nullptr);
    ASSERT_TRUE(obj->get_transform() != nullptr);
    ASSERT_TRUE(std::abs(obj->get_transform()->transform().position().x - 1.0f) < 1e-5f);
    ASSERT_EQ(find_tags(*obj).size(), static_cast<size_t>(1));
    ASSERT_EQ(find_tags(*obj)[0]->label, std::string("override-label"));

    coopa::scene::SceneObject* child = obj->find_descendant("Child1");
    ASSERT_TRUE(child != nullptr);
    ASSERT_EQ(find_tags(*child)[0]->label, std::string("child-label"));
}

void test_scene_inherit_component_merge_by_type() {
    using namespace scene_inherit_test;
    write_file("merge_by_type", "prefab.yaml",
        "object:\n"
        "  name: Base\n"
        "  components:\n"
        "    - type: TestTag\n"
        "      label: base-label\n"
        "      extra: base-extra\n");
    std::string scene_path = write_file("merge_by_type", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: prefab.yaml\n"
        "      components:\n"
        "        - type: TestTag\n"
        "          label: new-label\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    auto* tag = find_tags(*scene.find_object("Derived"))[0];
    ASSERT_EQ(tag->label, std::string("new-label"));
    ASSERT_EQ(tag->extra, std::string("base-extra"));
}

void test_scene_inherit_component_id_disambiguation() {
    using namespace scene_inherit_test;
    write_file("id_disambiguation", "prefab.yaml",
        "object:\n"
        "  name: Base\n"
        "  components:\n"
        "    - type: TestTag\n"
        "      id: A\n"
        "      label: A-base\n"
        "    - type: TestTag\n"
        "      id: B\n"
        "      label: B-base\n");
    std::string scene_path = write_file("id_disambiguation", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: prefab.yaml\n"
        "      components:\n"
        "        - type: TestTag\n"
        "          id: B\n"
        "          label: B-override\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    coopa::scene::SceneObject* obj = scene.find_object("Derived");
    ASSERT_EQ(find_tags(*obj).size(), static_cast<size_t>(2));
    ASSERT_EQ(find_tag_by_id(*obj, "A")->label, std::string("A-base"));
    ASSERT_EQ(find_tag_by_id(*obj, "B")->label, std::string("B-override"));
}

void test_scene_inherit_children_merge_and_append() {
    using namespace scene_inherit_test;
    write_file("children_merge", "prefab.yaml",
        "object:\n"
        "  name: Base\n"
        "  children:\n"
        "    - name: A\n"
        "      components: [ { type: TestTag, label: A-base } ]\n"
        "    - name: B\n"
        "      components: [ { type: TestTag, label: B-base } ]\n");
    std::string scene_path = write_file("children_merge", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: prefab.yaml\n"
        "      children:\n"
        "        - name: B\n"
        "          components: [ { type: TestTag, label: B-override } ]\n"
        "        - name: C\n"
        "          components: [ { type: TestTag, label: C-new } ]\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    coopa::scene::SceneObject* obj = scene.find_object("Derived");
    ASSERT_EQ(obj->children().size(), static_cast<size_t>(3));
    ASSERT_EQ(obj->children()[0]->name(), std::string("A"));
    ASSERT_EQ(obj->children()[1]->name(), std::string("B"));
    ASSERT_EQ(obj->children()[2]->name(), std::string("C"));
    ASSERT_EQ(find_tags(*obj->children()[0])[0]->label, std::string("A-base"));
    ASSERT_EQ(find_tags(*obj->children()[1])[0]->label, std::string("B-override"));
    ASSERT_EQ(find_tags(*obj->children()[2])[0]->label, std::string("C-new"));
}

void test_scene_inherit_remove() {
    using namespace scene_inherit_test;
    write_file("remove", "prefab.yaml",
        "object:\n"
        "  name: Base\n"
        "  components:\n"
        "    - type: TestTag\n"
        "      label: keep\n"
        "    - type: TestTag\n"
        "      id: doomed\n"
        "      label: remove-me\n"
        "  children:\n"
        "    - name: KeepChild\n"
        "    - name: RemoveChild\n");
    std::string scene_path = write_file("remove", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: prefab.yaml\n"
        "      components:\n"
        "        - type: TestTag\n"
        "          id: doomed\n"
        "          remove: true\n"
        "      children:\n"
        "        - name: RemoveChild\n"
        "          remove: true\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    coopa::scene::SceneObject* obj = scene.find_object("Derived");
    ASSERT_EQ(find_tags(*obj).size(), static_cast<size_t>(1));
    ASSERT_EQ(find_tags(*obj)[0]->label, std::string("keep"));
    ASSERT_EQ(obj->children().size(), static_cast<size_t>(1));
    ASSERT_EQ(obj->children()[0]->name(), std::string("KeepChild"));
}

void test_scene_inherit_scene_level() {
    using namespace scene_inherit_test;
    write_file("scene_level", "base_scene.yaml",
        "scene:\n"
        "  scene_name: BaseScene\n"
        "  root_objects:\n"
        "    - name: Canvas\n"
        "      components: [ { type: TestTag, label: canvas-base } ]\n"
        "    - name: Extra\n");
    std::string scene_path = write_file("scene_level", "derived_scene.yaml",
        "scene:\n"
        "  inherit_from: base_scene.yaml\n"
        "  scene_name: DerivedScene\n"
        "  root_objects:\n"
        "    - name: Canvas\n"
        "      components: [ { type: TestTag, label: canvas-override } ]\n"
        "    - name: New\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    ASSERT_EQ(scene.name(), std::string("DerivedScene"));
    ASSERT_EQ(find_tags(*scene.find_object("Canvas"))[0]->label, std::string("canvas-override"));
    ASSERT_TRUE(scene.find_object("Extra") != nullptr);
    ASSERT_TRUE(scene.find_object("New") != nullptr);
}

void test_scene_inherit_chain() {
    using namespace scene_inherit_test;
    write_file("chain", "a.yaml",
        "object:\n"
        "  name: A\n"
        "  components: [ { type: TestTag, label: A-label, extra: A-extra } ]\n");
    write_file("chain", "b.yaml",
        "object:\n"
        "  name: B\n"
        "  inherit_from: a.yaml\n"
        "  components: [ { type: TestTag, label: B-label } ]\n");
    std::string scene_path = write_file("chain", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Final\n"
        "      inherit_from: b.yaml\n"
        "      components: [ { type: TestTag, label: C-label } ]\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    auto* tag = find_tags(*scene.find_object("Final"))[0];
    ASSERT_EQ(tag->label, std::string("C-label"));
    ASSERT_EQ(tag->extra, std::string("A-extra"));
}

void test_scene_inherit_multiple_bases() {
    using namespace scene_inherit_test;
    write_file("multiple_bases", "base1.yaml",
        "object:\n"
        "  name: Base1\n"
        "  components: [ { type: TestTag, label: from-base1, extra: e1 } ]\n");
    write_file("multiple_bases", "base2.yaml",
        "object:\n"
        "  name: Base2\n"
        "  components: [ { type: TestTag, label: from-base2 } ]\n");
    std::string scene_path = write_file("multiple_bases", "scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Derived\n"
        "      inherit_from: [ base1.yaml, base2.yaml ]\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    auto* tag = find_tags(*scene.find_object("Derived"))[0];
    ASSERT_EQ(tag->label, std::string("from-base2"));
    ASSERT_EQ(tag->extra, std::string("e1"));
}

void test_scene_inherit_cycle_throws() {
    using namespace scene_inherit_test;
    write_file("cycle", "a.yaml", "object:\n  name: A\n  inherit_from: b.yaml\n");
    write_file("cycle", "b.yaml", "object:\n  name: B\n  inherit_from: a.yaml\n");
    std::string scene_path = write_file("cycle", "scene.yaml",
        "scene:\n  root_objects:\n    - name: X\n      inherit_from: a.yaml\n");

    bool threw = false;
    try {
        coopa::scene::SceneLoader::load(scene_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_scene_inherit_missing_file_throws() {
    using namespace scene_inherit_test;
    std::string scene_path = write_file("missing_file", "scene.yaml",
        "scene:\n  root_objects:\n    - name: X\n      inherit_from: does_not_exist.yaml\n");

    bool threw = false;
    try {
        coopa::scene::SceneLoader::load(scene_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_scene_inherit_asset_path_resolution() {
    using namespace scene_inherit_test;
    write_file("asset_path", "prefabs/note.txt", "hi");
    std::string note_path = (std::filesystem::temp_directory_path() /
        "libcoopa_scene_inherit_tests" / "asset_path" / "prefabs" / "note.txt").string();
    write_file("asset_path", "prefabs/labeled.yaml",
        "object:\n"
        "  name: Base\n"
        "  components: [ { type: TestAsset, file: note.txt } ]\n");
    std::string scene_path = write_file("asset_path", "scenes/scene.yaml",
        "scene:\n"
        "  root_objects:\n"
        "    - name: Widget\n"
        "      inherit_from: ../prefabs/labeled.yaml\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    coopa::scene::SceneObject* obj = scene.find_object("Widget");
    auto* asset = obj->get_component<TestAssetComponent>();
    ASSERT_TRUE(asset != nullptr);
    // Must resolve against prefabs/ (where note.txt actually lives), not
    // scenes/ (the including scene's own directory, which has no note.txt).
    ASSERT_EQ(std::filesystem::path(asset->resolved_path).filename().string(), std::string("note.txt"));
    ASSERT_TRUE(std::filesystem::exists(asset->resolved_path));
    ASSERT_EQ(std::filesystem::canonical(asset->resolved_path).string(), std::filesystem::canonical(note_path).string());
}

void test_scene_inherit_no_inherit_unchanged() {
    using namespace scene_inherit_test;
    std::string scene_path = write_file("no_inherit", "scene.yaml",
        "scene:\n"
        "  scene_name: Plain\n"
        "  root_objects:\n"
        "    - name: Solo\n"
        "      components:\n"
        "        - type: Transform\n"
        "          position: { x: 5.0, y: 6.0, z: 7.0 }\n"
        "        - type: TestTag\n"
        "          label: solo-label\n");

    coopa::scene::Scene scene = coopa::scene::SceneLoader::load(scene_path);
    ASSERT_EQ(scene.name(), std::string("Plain"));
    coopa::scene::SceneObject* obj = scene.find_object("Solo");
    ASSERT_TRUE(obj != nullptr);
    ASSERT_TRUE(std::abs(obj->get_transform()->transform().position().x - 5.0f) < 1e-5f);
    ASSERT_EQ(find_tags(*obj)[0]->label, std::string("solo-label"));
}

// ---------------------------------------------------------
// Scene phase pipeline tests
// ---------------------------------------------------------
//
// These guard the entire compatibility risk of the coopa::scene::ISceneSystem
// pipeline: Scene::update()/late_update() used to be two hardcoded Component
// tree walks; they are now an ordered list of systems, of which the walks are
// just the two built-ins. Every existing consumer (blendy/toyengine call
// update() only; several uicoopa tests call late_update() standalone; two
// callers call both) must see byte-identical behavior.

namespace scene_pipeline_test {

/** @brief Appends its name to a shared log every time it executes — for asserting call order. */
class RecordingSystem : public coopa::scene::ISceneSystem {
public:
    RecordingSystem(std::string name, std::vector<std::string>* log)
        : name_(std::move(name)), log_(log) {}
    void execute(coopa::scene::Scene&, const coopa::scene::FrameContext&) override {
        log_->push_back(name_);
    }
    const char* system_name() const override { return name_.c_str(); }
private:
    std::string name_;
    std::vector<std::string>* log_;
};

/** @brief Records whether FrameContext::jobs was null the last time it executed. */
class JobsNullCheckSystem : public coopa::scene::ISceneSystem {
public:
    explicit JobsNullCheckSystem(bool* jobs_was_null) : jobs_was_null_(jobs_was_null) {}
    void execute(coopa::scene::Scene&, const coopa::scene::FrameContext& ctx) override {
        *jobs_was_null_ = (ctx.jobs == nullptr);
    }
    const char* system_name() const override { return "JobsNullCheck"; }
private:
    bool* jobs_was_null_;
};

/**
 * @brief Every execute() call creates a handle, submits one trivial job onto
 *        it, waits for it, and closes it — exercising the exact JobEngine
 *        usage pattern AnimationSystem follows, without ever calling
 *        begin_frame()/end_frame() itself (that boundary is diagnostics-only
 *        now and optional -- see coopa/job/handle.h).
 */
class JobUsingSystem : public coopa::scene::ISceneSystem {
public:
    explicit JobUsingSystem(bool* all_valid) : all_valid_(all_valid) {}
    void execute(coopa::scene::Scene&, const coopa::scene::FrameContext& ctx) override {
        if (!ctx.jobs) { *all_valid_ = false; return; }
        coopa::job::JobHandle h = ctx.jobs->create_handle();
        if (!h.is_valid()) { *all_valid_ = false; return; }
        ctx.jobs->submit([] {}, 0, h);
        ctx.jobs->wait_for(h);
        h.close();
    }
    const char* system_name() const override { return "JobUsing"; }
private:
    bool* all_valid_;
};

/** @brief Increments on_attach()/on_detach() counters, for verifying add_system()/remove_system(). */
class AttachDetachSystem : public coopa::scene::ISceneSystem {
public:
    AttachDetachSystem(int* attach_count, int* detach_count)
        : attach_count_(attach_count), detach_count_(detach_count) {}
    void on_attach(coopa::scene::Scene&) override { ++(*attach_count_); }
    void on_detach(coopa::scene::Scene&) override { ++(*detach_count_); }
    void execute(coopa::scene::Scene&, const coopa::scene::FrameContext&) override {}
    const char* system_name() const override { return "AttachDetach"; }
private:
    int* attach_count_;
    int* detach_count_;
};

/** @brief A test-only Component whose update() counts how many times it ran. */
class CountingComponent : public coopa::scene::Component {
public:
    std::string type_name() const override { return "Counting"; }
    int update_count = 0;
    void update(float) override { ++update_count; }
};

} // namespace scene_pipeline_test

void test_scene_phase_order() {
    using namespace scene_pipeline_test;

    coopa::scene::Scene scene("PhaseOrder");
    std::vector<std::string> log;
    scene.add_system(std::make_unique<RecordingSystem>("mid", &log), 250);
    scene.add_system(std::make_unique<RecordingSystem>("early", &log), 150);
    scene.add_system(std::make_unique<RecordingSystem>("late", &log), 350);
    scene.update(0.016f); // all three are < LateBehaviour(400), so all run here, in ascending order.
    ASSERT_EQ(log.size(), static_cast<size_t>(3));
    ASSERT_EQ(log[0], std::string("early"));
    ASSERT_EQ(log[1], std::string("mid"));
    ASSERT_EQ(log[2], std::string("late"));

    coopa::scene::Scene scene2("EqualOrder");
    std::vector<std::string> log2;
    scene2.add_system(std::make_unique<RecordingSystem>("first", &log2), 250);
    scene2.add_system(std::make_unique<RecordingSystem>("second", &log2), 250);
    scene2.update(0.016f);
    ASSERT_EQ(log2.size(), static_cast<size_t>(2));
    ASSERT_EQ(log2[0], std::string("first"));
    ASSERT_EQ(log2[1], std::string("second"));
}

void test_scene_update_late_update_split() {
    using namespace scene_pipeline_test;

    coopa::scene::Scene scene("Split");
    std::vector<std::string> log;
    scene.add_system(std::make_unique<RecordingSystem>("before_late", &log), 350); // < LateBehaviour(400)
    scene.add_system(std::make_unique<RecordingSystem>("after_late", &log), 450);  // >= LateBehaviour(400)

    scene.update(0.016f);
    ASSERT_EQ(log.size(), static_cast<size_t>(1));
    ASSERT_EQ(log[0], std::string("before_late"));

    scene.late_update(0.016f);
    ASSERT_EQ(log.size(), static_cast<size_t>(2));
    ASSERT_EQ(log[1], std::string("after_late"));
}

void test_scene_late_update_standalone() {
    using namespace scene_pipeline_test;

    coopa::scene::Scene scene("StandaloneLate");
    std::vector<std::string> log;
    scene.add_system(std::make_unique<RecordingSystem>("late_only", &log), 450);

    scene.late_update(0.016f); // no preceding update() this frame
    ASSERT_EQ(log.size(), static_cast<size_t>(1));
    ASSERT_EQ(log[0], std::string("late_only"));
    ASSERT_TRUE(scene.job_engine() == nullptr); // never touched, since none was ever installed
}

void test_scene_frame_boundary_update_only() {
    using namespace scene_pipeline_test;

    // Regression coverage for the old design: Scene::update() used to have
    // to reset the engine's frame boundary itself every call for a caller
    // that only ever calls update() and never late_update() (the
    // blendy/toyengine shape), since CounterPool was an untagged bump
    // allocator only ever reclaimable in bulk. Now that handles are
    // individually reclaimed (see coopa/job/handle.h), Scene no longer
    // touches the frame boundary at all -- this runs far more iterations
    // than the tiny handle pool capacity (4) could hold at once, relying
    // entirely on JobUsingSystem's own handle.close() each call to keep the
    // pool from ever exhausting.
    coopa::job::JobEngine engine(2, 64, 4);
    coopa::scene::Scene scene("FrameBoundaryUpdateOnly");
    scene.set_job_engine(&engine);
    bool all_valid = true;
    scene.add_system(std::make_unique<JobUsingSystem>(&all_valid), 250);

    for (int i = 0; i < 200; ++i) {
        scene.update(0.016f);
    }
    ASSERT_TRUE(all_valid);
    engine.shutdown();
}

void test_scene_null_job_engine_runs_inline() {
    using namespace scene_pipeline_test;

    coopa::scene::Scene scene("NoEngine");
    bool jobs_was_null = false;
    scene.add_system(std::make_unique<JobsNullCheckSystem>(&jobs_was_null), 250);
    scene.update(0.016f);
    ASSERT_TRUE(jobs_was_null);
}

void test_scene_add_remove_system() {
    using namespace scene_pipeline_test;

    coopa::scene::Scene scene("AddRemove");
    int attach = 0, detach = 0;
    scene.add_system(std::make_unique<AttachDetachSystem>(&attach, &detach), 250);
    ASSERT_EQ(attach, 1);
    ASSERT_EQ(detach, 0);
    ASSERT_TRUE(scene.remove_system("AttachDetach"));
    ASSERT_EQ(detach, 1);
    ASSERT_TRUE(scene.find_system("AttachDetach") == nullptr);
    ASSERT_TRUE(!scene.remove_system("AttachDetach")); // already removed

    auto obj = std::make_unique<coopa::scene::SceneObject>("Obj");
    auto* counter = obj->add_component<CountingComponent>();
    scene.add_root_object(std::move(obj));
    scene.update(0.016f);
    ASSERT_EQ(counter->update_count, 1);

    ASSERT_TRUE(scene.remove_system("Behaviour"));
    scene.update(0.016f);
    ASSERT_EQ(counter->update_count, 1); // unchanged: the built-in Component::update() walk no longer runs
}

void test_scene_move_preserves_systems() {
    using namespace scene_pipeline_test;

    coopa::job::JobEngine engine(2);
    std::vector<std::string> log;
    coopa::scene::Scene scene("MoveSrc");
    scene.set_job_engine(&engine);
    scene.add_system(std::make_unique<RecordingSystem>("sys", &log), 250);

    coopa::scene::Scene moved(std::move(scene));
    ASSERT_TRUE(moved.job_engine() == &engine);
    moved.update(0.016f);
    ASSERT_EQ(log.size(), static_cast<size_t>(1));
    moved.late_update(0.016f);
    engine.shutdown();
}

// ---------------------------------------------------------
// Multi-scene processing, deferred SceneCommandBuffer, and
// TransformSystem (Job system rework, part 2: Scenes)
// ---------------------------------------------------------

namespace scene_jobify_test {

/// @brief Fans out one chunk per JobEngine worker, each recording an add_root_object()
/// into its OWN SceneCommandBuffer -- exercises FrameContext::commands/worker_index.
class SpawnPerWorkerSystem : public coopa::scene::ISceneSystem {
public:
    const char* system_name() const override { return "SpawnPerWorker"; }
    void execute(coopa::scene::Scene& scene, const coopa::scene::FrameContext& ctx) override {
        if (!ctx.jobs || ctx.jobs->worker_count() == 0) return;
        ctx.jobs->parallel_for_blocking(ctx.jobs->worker_count(), 1,
            [&scene](size_t start, size_t end, const coopa::job::JobContext& jctx) {
                for (size_t i = start; i < end; ++i) {
                    auto obj = std::make_unique<coopa::scene::SceneObject>(
                        "FromWorker" + std::to_string(jctx.worker_index));
                    scene.commands_for(jctx.worker_index).add_root_object(std::move(obj));
                }
            });
    }
};

} // namespace scene_jobify_test

void test_scene_command_buffer_flushes_in_ascending_worker_index_order() {
    // Deterministic: records directly into each worker's buffer (any thread
    // may call commands_for(i) -- only the CONVENTION is that worker i
    // writes its own buffer, nothing stops a test setting them up directly)
    // in REVERSE index order, to prove flush_commands() applies them in
    // ascending buffer-index order rather than recording order.
    coopa::job::JobEngine engine(4);
    coopa::scene::Scene scene("CommandBufferOrder");
    scene.set_job_engine(&engine); // sizes the per-worker buffers

    for (uint32_t i = engine.worker_count(); i-- > 0; ) {
        auto obj = std::make_unique<coopa::scene::SceneObject>("FromWorker" + std::to_string(i));
        scene.commands_for(i).add_root_object(std::move(obj));
    }

    size_t before = scene.root_objects().size();
    scene.flush_commands();
    size_t after = scene.root_objects().size();

    ASSERT_EQ(after - before, static_cast<size_t>(engine.worker_count()));
    for (uint32_t i = 0; i < engine.worker_count(); ++i) {
        ASSERT_EQ(scene.root_objects()[before + i]->name(), std::string("FromWorker") + std::to_string(i));
    }
    engine.shutdown();
}

void test_scene_command_buffer_parallel_writes_all_land_exactly_once() {
    // Realistic usage: a system fans out via parallel_for(), each chunk
    // writing into its OWN buffer via commands_for(jctx.worker_index). Which
    // physical worker ends up running a given chunk is NOT guaranteed
    // one-to-one with the chunk's logical index (work-stealing may let one
    // fast worker grab more than one chunk) -- so this only asserts the
    // aggregate invariant that matters: every recorded command lands exactly
    // once, with none lost or duplicated, regardless of that mapping.
    using namespace scene_jobify_test;

    coopa::job::JobEngine engine(4);
    coopa::scene::Scene scene("CommandBufferParallel");
    scene.set_job_engine(&engine);
    scene.add_system(std::make_unique<SpawnPerWorkerSystem>(), 250);

    size_t before = scene.root_objects().size();
    scene.update(0.016f);
    scene.late_update(0.016f); // flush_commands() runs here

    size_t spawned_count = 0;
    for (auto& root : scene.root_objects()) {
        if (root->name().rfind("FromWorker", 0) == 0) ++spawned_count;
    }
    ASSERT_EQ(spawned_count, static_cast<size_t>(engine.worker_count()));
    ASSERT_EQ(scene.root_objects().size(), before + spawned_count);

    engine.shutdown();
}

void test_scene_manager_concurrent_scenes_match_serial() {
    using namespace scene_pipeline_test;

    coopa::job::JobEngine engine(4);
    coopa::scene::SceneManager mgr;
    mgr.set_job_engine(&engine);

    std::vector<CountingComponent*> counters;
    for (int s = 0; s < 6; ++s) {
        auto scene = std::make_unique<coopa::scene::Scene>("S" + std::to_string(s));
        for (int i = 0; i < 20; ++i) {
            auto obj = std::make_unique<coopa::scene::SceneObject>("Obj");
            counters.push_back(obj->add_component<CountingComponent>());
            scene->add_root_object(std::move(obj));
        }
        mgr.add_scene(std::move(scene));
    }

    // Six scenes concurrently processed across four workers, three frames --
    // if two of Scene's methods ever ran concurrently on the SAME Scene,
    // COOPA_SCENE_THREAD_CHECKS (on for this test binary) would abort.
    for (int frame = 0; frame < 3; ++frame) {
        mgr.begin_frame();
        mgr.update(0.016f);
        mgr.late_update(0.016f);
        mgr.end_frame();
    }

    for (auto* c : counters) {
        ASSERT_EQ(c->update_count, 3);
    }
    engine.shutdown();
}

void test_scene_manager_no_engine_additive_active_remove() {
    using namespace scene_pipeline_test;

    coopa::scene::SceneManager mgr; // no engine installed -- serial fallback path
    std::vector<CountingComponent*> counters;
    coopa::scene::Scene* first = nullptr;
    std::vector<coopa::scene::Scene*> raws;
    for (int s = 0; s < 3; ++s) {
        auto scene = std::make_unique<coopa::scene::Scene>("S" + std::to_string(s));
        auto obj = std::make_unique<coopa::scene::SceneObject>("Obj");
        counters.push_back(obj->add_component<CountingComponent>());
        scene->add_root_object(std::move(obj));
        coopa::scene::Scene* raw = mgr.add_scene(std::move(scene));
        raws.push_back(raw);
        if (s == 0) first = raw;
    }
    ASSERT_TRUE(mgr.has_scene());
    ASSERT_TRUE(&mgr.get_active_scene() == first); // first scene added becomes active
    ASSERT_EQ(mgr.scenes().size(), static_cast<size_t>(3));

    mgr.update(0.016f);
    mgr.late_update(0.016f);
    for (auto* c : counters) ASSERT_EQ(c->update_count, 1);

    mgr.set_scene_active(raws[1], false);
    mgr.update(0.016f);
    mgr.late_update(0.016f);
    ASSERT_EQ(counters[0]->update_count, 2);
    ASSERT_EQ(counters[1]->update_count, 1); // unchanged -- inactive
    ASSERT_EQ(counters[2]->update_count, 2);

    ASSERT_TRUE(mgr.remove_scene(raws[1]));
    ASSERT_EQ(mgr.scenes().size(), static_cast<size_t>(2));
    ASSERT_TRUE(!mgr.remove_scene(raws[1])); // already removed
}

void test_transform_set_rotation_euler_unaffected_by_quat_support() {
    coopa::util::Transform t;
    t.set_rotation({15.0f, 30.0f, 45.0f});
    glm::mat4 before = t.get_world_matrix();

    // Round-trip through the quaternion path and back to Euler must reproduce the same
    // matrix -- set_rotation_quat()'s Euler re-derivation is the exact analytic inverse of
    // the eulerAngleZYX composition recompute_() uses.
    glm::quat q = t.rotation_quat();
    t.set_rotation_quat(q);
    glm::vec3 degrees_after_quat = t.rotation_degrees();
    t.set_rotation(degrees_after_quat);
    glm::mat4 after = t.get_world_matrix();

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_TRUE(std::abs(before[r][c] - after[r][c]) < 1e-4f);
        }
    }
}

void test_transform_set_rotation_quat_matches_mat4_cast() {
    coopa::util::Transform t;
    glm::quat q = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
    t.set_rotation_quat(q);

    glm::mat4 expected = glm::mat4_cast(q);
    glm::mat4 actual = t.get_world_matrix(); // no position/scale set -> pure rotation matrix
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_TRUE(std::abs(expected[r][c] - actual[r][c]) < 1e-4f);
        }
    }

    // set_rotation(vec3) must clear use_quat_, reverting to ordinary Euler authoring.
    t.set_rotation({0.0f, 0.0f, 0.0f});
    glm::mat4 identity_rot = t.get_world_matrix();
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float expected_identity = (r == c) ? 1.0f : 0.0f;
            ASSERT_TRUE(std::abs(identity_rot[r][c] - expected_identity) < 1e-4f);
        }
    }
}

void test_transform_system_matches_lazy_resolve() {
    using namespace coopa::scene;

    auto build = [](Scene& scene) {
        auto root = std::make_unique<SceneObject>("Root");
        auto* root_tc = root->add_component<TransformComponent>();
        root_tc->transform().set_position({1.0f, 2.0f, 3.0f});
        for (int i = 0; i < 5; ++i) {
            auto child = std::make_unique<SceneObject>("Child" + std::to_string(i));
            auto* child_tc = child->add_component<TransformComponent>();
            child_tc->set_parent_transform(&root_tc->transform());
            child_tc->transform().set_position({static_cast<float>(i), 0.0f, 0.0f});
            root->add_child(std::move(child));
        }
        scene.add_root_object(std::move(root));
    };

    Scene lazy_scene("Lazy");
    build(lazy_scene);
    std::vector<glm::mat4> lazy_matrices;
    for (auto& child : lazy_scene.root_objects()[0]->children()) {
        lazy_matrices.push_back(child->get_transform()->get_world_matrix());
    }

    coopa::job::JobEngine engine(4);
    Scene resolved_scene("Resolved");
    build(resolved_scene);
    resolved_scene.set_job_engine(&engine);
    install_transform_system(resolved_scene);
    resolved_scene.start();
    resolved_scene.update(0.016f);  // runs TransformSystem
    resolved_scene.late_update(0.016f);

    size_t i = 0;
    for (auto& child : resolved_scene.root_objects()[0]->children()) {
        ASSERT_TRUE(!child->get_transform()->transform().is_dirty());
        glm::mat4 resolved = child->get_transform()->transform().world_matrix();
        glm::mat4 expected = lazy_matrices[i++];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                ASSERT_TRUE(std::abs(resolved[r][c] - expected[r][c]) < 1e-6f);
            }
        }
    }
    engine.shutdown();
}

void test_transform_system_concurrent_reads_are_race_free() {
    using namespace coopa::scene;

    Scene scene("ConcurrentReads");
    auto root = std::make_unique<SceneObject>("Root");
    auto* root_tc = root->add_component<TransformComponent>();
    constexpr int kChildren = 64;
    for (int i = 0; i < kChildren; ++i) {
        auto child = std::make_unique<SceneObject>("Child" + std::to_string(i));
        auto* tc = child->add_component<TransformComponent>();
        tc->set_parent_transform(&root_tc->transform());
        tc->transform().set_position({static_cast<float>(i), 0.0f, 0.0f});
        root->add_child(std::move(child));
    }
    scene.add_root_object(std::move(root));

    coopa::job::JobEngine engine(4);
    scene.set_job_engine(&engine);
    install_transform_system(scene);
    scene.start();
    scene.update(0.016f);
    scene.late_update(0.016f);

    auto& children = scene.root_objects()[0]->children();
    std::vector<float> expected_x(children.size());
    for (size_t i = 0; i < children.size(); ++i) {
        expected_x[i] = children[i]->get_transform()->transform().world_matrix()[3].x;
    }

    // Every transform is clean after the resolve pass (world_matrix() itself
    // asserts this in debug builds) -- read every child's world_matrix()
    // concurrently, many times over, from several jobs at once. A race in
    // the underlying cache would corrupt a value or trip that assert;
    // recording a mismatch here (rather than asserting from inside the job
    // body) keeps a failure from becoming an uncaught-exception abort on a
    // worker thread.
    std::atomic<bool> mismatch{false};
    engine.parallel_for_blocking(children.size(), 1, [&](size_t start, size_t end) {
        for (int rep = 0; rep < 50; ++rep) {
            for (size_t i = start; i < end; ++i) {
                float got = children[i]->get_transform()->transform().world_matrix()[3].x;
                if (std::abs(got - expected_x[i]) >= 1e-6f) {
                    mismatch.store(true, std::memory_order_relaxed);
                }
            }
        }
    });
    ASSERT_TRUE(!mismatch.load());
    engine.shutdown();
}

// ---------------------------------------------------------
// Animation module tests (coopa::anim)
// ---------------------------------------------------------

namespace animation_test {

/** @brief A minimal test-only component exposing one vec3 and one float property to animate. */
class TestProbeComponent : public coopa::scene::Component {
public:
    std::string type_name() const override { return "TestProbe"; }
    glm::vec3 pos{0.0f};
    float alpha = 0.0f;

    const glm::vec3& position() const { return pos; }
    void set_position(const glm::vec3& p) { pos = p; }
    float get_alpha() const { return alpha; }
    void set_alpha(float a) { alpha = a; }
};

void register_properties() {
    auto& reg = coopa::anim::AnimatedPropertyRegistry::instance();
    reg.register_vec<TestProbeComponent, glm::vec3>(
        "TestProbe", "pos", &TestProbeComponent::position, &TestProbeComponent::set_position);
    reg.register_float<TestProbeComponent>(
        "TestProbe", "alpha", &TestProbeComponent::get_alpha, &TestProbeComponent::set_alpha);
}

// Fixtures live under a fresh per-test subdirectory of the system temp dir,
// mirroring scene_inherit_test::write_file()'s reasoning.
std::string write_file(const std::string& test_name, const std::string& relative_path, const std::string& content) {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "libcoopa_animation_tests" / test_name / relative_path;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
    out.close();
    return path.string();
}

} // namespace animation_test

void test_animation_curve_sampling() {
    using coopa::anim::AnimationCurve;
    using coopa::anim::Interpolation;
    using coopa::anim::Keyframe;

    AnimationCurve curve;
    curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    curve.sort_keys();

    float out[1];
    curve.sample(0.0f, 1, out);
    ASSERT_TRUE(std::abs(out[0] - 0.0f) < 1e-5f);
    curve.sample(1.0f, 1, out);
    ASSERT_TRUE(std::abs(out[0] - 10.0f) < 1e-5f);
    curve.sample(0.5f, 1, out);
    ASSERT_TRUE(std::abs(out[0] - 5.0f) < 1e-5f);
    curve.sample(-1.0f, 1, out); // clamps before the first key
    ASSERT_TRUE(std::abs(out[0] - 0.0f) < 1e-5f);
    curve.sample(5.0f, 1, out); // clamps after the last key
    ASSERT_TRUE(std::abs(out[0] - 10.0f) < 1e-5f);

    AnimationCurve empty;
    empty.sample(0.5f, 1, out);
    ASSERT_TRUE(std::abs(out[0]) < 1e-5f);

    AnimationCurve step;
    step.add_key(Keyframe{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Step});
    step.add_key(Keyframe{1.0f, {2.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    step.sort_keys();
    step.sample(0.5f, 1, out); // Step holds the leaving key's value for the whole segment
    ASSERT_TRUE(std::abs(out[0] - 1.0f) < 1e-5f);

    AnimationCurve eio;
    eio.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    eio.add_key(Keyframe{1.0f, {1.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    eio.sort_keys();
    eio.sample(0.5f, 1, out); // EaseInOut is symmetric: exactly 0.5 at the midpoint
    ASSERT_TRUE(std::abs(out[0] - 0.5f) < 1e-5f);
}

void test_animation_property_registry() {
    using namespace animation_test;

    auto& reg = coopa::anim::AnimatedPropertyRegistry::instance();
    uint8_t mask = 0;
    const auto* pos = reg.find("TestProbe", "pos", &mask);
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(static_cast<int>(mask), 0x0F);
    ASSERT_EQ(static_cast<int>(pos->component_count), 3);

    const auto* pos_y = reg.find("TestProbe", "pos.y", &mask);
    ASSERT_TRUE(pos_y == pos); // same underlying property, channel-suffixed
    ASSERT_EQ(static_cast<int>(mask), 0x02);

    ASSERT_TRUE(reg.find("TestProbe", "nonexistent") == nullptr);
    ASSERT_TRUE(reg.find("NoSuchComponent", "pos") == nullptr);

    auto names = reg.properties_for("TestProbe");
    ASSERT_TRUE(std::find(names.begin(), names.end(), "pos") != names.end());
    ASSERT_TRUE(std::find(names.begin(), names.end(), "alpha") != names.end());
}

void test_animator_plays_clip_on_transform() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("PlayClip");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Loop;
    clip->set_explicit_length(1.0f);
    AnimationTrack track;
    track.property = "position";
    track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.sort_keys();
    clip->tracks.push_back(track);
    animator->add_state("move", clip);
    animator->auto_play = "move";

    install_animation_system(scene);
    scene.start();
    scene.update(0.5f);
    scene.late_update(0.5f);

    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-4f);
}

void test_animator_channel_mask_preserves_other_channels() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("ChannelMask");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));
    scene.find_object("Obj")->get_transform()->transform().set_position({1.0f, 2.0f, 3.0f});

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Loop;
    clip->set_explicit_length(2.0f);
    AnimationTrack track;
    track.property = "position.y"; // narrows to a single channel
    track.curve.add_key(Keyframe{0.0f, {99.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.add_key(Keyframe{2.0f, {99.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.sort_keys();
    clip->tracks.push_back(track);
    animator->add_state("s", clip);
    animator->auto_play = "s";

    install_animation_system(scene);
    scene.start();
    scene.update(0.5f);
    scene.late_update(0.5f);

    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 1.0f) < 1e-5f); // untouched
    ASSERT_TRUE(std::abs(pos.y - 99.0f) < 1e-5f); // driven
    ASSERT_TRUE(std::abs(pos.z - 3.0f) < 1e-5f); // untouched
}

void test_animator_dedups_bindings_no_clobber() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("Dedup");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Loop;
    clip->set_explicit_length(2.0f);
    AnimationTrack tx;
    tx.property = "position.x";
    tx.curve.add_key(Keyframe{0.0f, {5.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tx.curve.add_key(Keyframe{2.0f, {5.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tx.curve.sort_keys();
    AnimationTrack tz;
    tz.property = "position.z";
    tz.curve.add_key(Keyframe{0.0f, {7.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tz.curve.add_key(Keyframe{2.0f, {7.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tz.curve.sort_keys();
    clip->tracks.push_back(tx);
    clip->tracks.push_back(tz);
    animator->add_state("s", clip);
    animator->auto_play = "s";

    install_animation_system(scene);
    scene.start();
    ASSERT_EQ(animator->binding_count(), static_cast<size_t>(1)); // deduped into ONE binding

    scene.update(0.5f);
    scene.late_update(0.5f);
    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-5f);
    ASSERT_TRUE(std::abs(pos.z - 7.0f) < 1e-5f);
}

void test_animator_wrap_modes() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    // Once: clamps at length, fires on_state_finished exactly once.
    {
        Scene scene("WrapOnce");
        auto obj = std::make_unique<SceneObject>("Obj");
        obj->add_component<TransformComponent>();
        auto* animator = obj->add_component<Animator>();
        scene.add_root_object(std::move(obj));

        auto clip = std::make_shared<AnimationClip>();
        clip->wrap = WrapMode::Once;
        clip->set_explicit_length(1.0f);
        AnimationTrack track;
        track.property = "position";
        track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.sort_keys();
        clip->tracks.push_back(track);
        animator->add_state("once", clip);
        animator->auto_play = "once";

        install_animation_system(scene);
        scene.start();

        int finished_count = 0;
        animator->on_state_finished.connect([&](const std::string&) { ++finished_count; });

        scene.update(1.5f); // past length=1.0
        scene.late_update(1.5f);
        ASSERT_EQ(finished_count, 1);
        auto pos = scene.find_object("Obj")->get_transform()->transform().position();
        ASSERT_TRUE(std::abs(pos.x - 10.0f) < 1e-4f); // holds the final pose

        scene.update(0.2f); // still finished; must not re-fire
        scene.late_update(0.2f);
        ASSERT_EQ(finished_count, 1);
    }

    // Loop: wraps back to 0.
    {
        Scene scene("WrapLoop");
        auto obj = std::make_unique<SceneObject>("Obj");
        obj->add_component<TransformComponent>();
        auto* animator = obj->add_component<Animator>();
        scene.add_root_object(std::move(obj));

        auto clip = std::make_shared<AnimationClip>();
        clip->wrap = WrapMode::Loop;
        clip->set_explicit_length(1.0f);
        AnimationTrack track;
        track.property = "position";
        track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.sort_keys();
        clip->tracks.push_back(track);
        animator->add_state("loop", clip);
        animator->auto_play = "loop";

        install_animation_system(scene);
        scene.start();
        scene.update(1.25f); // wraps to 0.25 -> x == 2.5
        scene.late_update(1.25f);
        auto pos = scene.find_object("Obj")->get_transform()->transform().position();
        ASSERT_TRUE(std::abs(pos.x - 2.5f) < 1e-3f);
    }

    // PingPong: reflects back and forth between 0 and length.
    {
        Scene scene("WrapPingPong");
        auto obj = std::make_unique<SceneObject>("Obj");
        obj->add_component<TransformComponent>();
        auto* animator = obj->add_component<Animator>();
        scene.add_root_object(std::move(obj));

        auto clip = std::make_shared<AnimationClip>();
        clip->wrap = WrapMode::PingPong;
        clip->set_explicit_length(1.0f);
        AnimationTrack track;
        track.property = "position";
        track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
        track.curve.sort_keys();
        clip->tracks.push_back(track);
        animator->add_state("pp", clip);
        animator->auto_play = "pp";

        install_animation_system(scene);
        scene.start();
        scene.update(1.25f); // period=2.0; raw t=1.25 -> reflected sample_time=0.75 -> x==7.5
        scene.late_update(1.25f);
        auto pos = scene.find_object("Obj")->get_transform()->transform().position();
        ASSERT_TRUE(std::abs(pos.x - 7.5f) < 1e-3f);
    }
}

void test_animator_crossfade_blends() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("Crossfade");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip_a = std::make_shared<AnimationClip>();
    clip_a->wrap = WrapMode::Loop;
    clip_a->set_explicit_length(10.0f);
    AnimationTrack ta;
    ta.property = "position";
    ta.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    ta.curve.add_key(Keyframe{10.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    ta.curve.sort_keys();
    clip_a->tracks.push_back(ta);

    auto clip_b = std::make_shared<AnimationClip>();
    clip_b->wrap = WrapMode::Loop;
    clip_b->set_explicit_length(10.0f);
    AnimationTrack tb;
    tb.property = "position";
    tb.curve.add_key(Keyframe{0.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tb.curve.add_key(Keyframe{10.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tb.curve.sort_keys();
    clip_b->tracks.push_back(tb);

    animator->add_state("A", clip_a);
    animator->add_state("B", clip_b);
    animator->auto_play = "A";

    install_animation_system(scene);
    scene.start();
    animator->crossfade("B", 1.0f);

    scene.update(0.5f);
    scene.late_update(0.5f); // 50% faded
    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-3f);

    scene.update(0.6f);
    scene.late_update(0.6f); // past the full fade -> B only
    pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 10.0f) < 1e-3f);
}

void test_animator_binds_by_name_and_path() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("BindByNameAndPath");
    auto root = std::make_unique<SceneObject>("Root");
    root->add_component<TransformComponent>();
    auto* animator = root->add_component<Animator>();

    auto child = std::make_unique<SceneObject>("Child");
    child->add_component<TransformComponent>();
    auto grandchild = std::make_unique<SceneObject>("Grandchild");
    grandchild->add_component<TransformComponent>();
    SceneObject* grandchild_raw = child->add_child(std::move(grandchild));
    root->add_child(std::move(child));

    auto sibling = std::make_unique<SceneObject>("Sibling");
    sibling->add_component<TransformComponent>();

    SceneObject* root_raw = scene.add_root_object(std::move(root));
    (void)root_raw;
    scene.add_root_object(std::move(sibling));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Once;
    clip->set_explicit_length(1.0f);

    AnimationTrack path_track; // "Child/Grandchild" -- segment-walked descendant path
    path_track.object_path = "Child/Grandchild";
    path_track.property = "position";
    path_track.curve.add_key(Keyframe{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    path_track.curve.add_key(Keyframe{1.0f, {1.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    path_track.curve.sort_keys();
    clip->tracks.push_back(path_track);

    AnimationTrack sibling_track; // "Sibling" -- not a descendant, resolved via Scene::find_object
    sibling_track.object_path = "Sibling";
    sibling_track.property = "position";
    sibling_track.curve.add_key(Keyframe{0.0f, {2.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    sibling_track.curve.add_key(Keyframe{1.0f, {2.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    sibling_track.curve.sort_keys();
    clip->tracks.push_back(sibling_track);

    AnimationTrack missing_track; // unresolvable target -- must be dropped gracefully, not throw
    missing_track.object_path = "DoesNotExist";
    missing_track.property = "position";
    missing_track.curve.add_key(Keyframe{0.0f, {3.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    missing_track.curve.sort_keys();
    clip->tracks.push_back(missing_track);

    animator->add_state("s", clip);
    animator->auto_play = "s";

    install_animation_system(scene);
    scene.start();
    scene.update(0.1f);
    scene.late_update(0.1f);

    ASSERT_TRUE(std::abs(grandchild_raw->get_transform()->transform().position().x - 1.0f) < 1e-4f);
    ASSERT_TRUE(std::abs(scene.find_object("Sibling")->get_transform()->transform().position().x - 2.0f) < 1e-4f);
    // Only 2 of the 3 tracks resolved to a binding; the unresolvable one was dropped, not thrown.
    ASSERT_EQ(animator->binding_count(), static_cast<size_t>(2));
}

void test_animator_lazy_binds_when_initially_inactive() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    // SceneObject::start() returns early for inactive objects, so an Animator
    // on an object inactive at Scene::start() time never receives its own
    // start() -- advance_()'s "if (!bound_) rebind()" is the fallback that
    // must catch this once the object is reactivated.
    Scene scene("LazyBindInactive");
    auto obj = std::make_unique<SceneObject>("Obj", /*active=*/false);
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Once;
    clip->set_explicit_length(1.0f);
    AnimationTrack track;
    track.property = "position";
    track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.add_key(Keyframe{1.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.sort_keys();
    clip->tracks.push_back(track);
    animator->add_state("s", clip);
    animator->auto_play = "s";

    install_animation_system(scene);
    scene.start(); // Obj is inactive -> Animator::start() never runs.
    ASSERT_EQ(animator->binding_count(), static_cast<size_t>(0));

    scene.find_object("Obj")->set_active(true);
    // Since start() never ran, auto_play never triggered play() either --
    // AnimationSystem::execute() still calls advance_()/evaluate_()/apply_()
    // for every Animator its cached list found (which does include this one,
    // since get_components<T>() re-scans on the system's next execute() only
    // if refresh()d -- here it's still fresh from before start() flipped
    // active, so this exercises the lazy rebind path specifically).
    animator->play("s");
    scene.update(0.5f);
    scene.late_update(0.5f);

    ASSERT_TRUE(animator->binding_count() > 0);
    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-3f);
}

void test_animation_rotation_does_not_wrap() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("RotationNoWrap");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Once;
    clip->set_explicit_length(1.0f);
    AnimationTrack track;
    track.property = "rotation";
    track.curve.add_key(Keyframe{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    track.curve.add_key(Keyframe{1.0f, {0.0f, 0.0f, 720.0f, 1.0f}, Interpolation::Linear}); // two full spins
    track.curve.sort_keys();
    clip->tracks.push_back(track);
    animator->add_state("s", clip);
    animator->auto_play = "s";

    install_animation_system(scene);
    scene.start();
    scene.update(0.5f);
    scene.late_update(0.5f);

    // At the midpoint, z must be 360 (halfway through two full spins), NOT 0
    // (which a shortest-path wrap would incorrectly produce).
    auto rot = scene.find_object("Obj")->get_transform()->transform().rotation_degrees();
    ASSERT_TRUE(std::abs(rot.z - 360.0f) < 1e-2f);
}

void test_animation_clip_loader_yaml() {
    using namespace animation_test;
    using namespace coopa::anim;

    coopa::asset::AssetManager assets;
    assets.register_loader<AnimationClip>(std::make_unique<AnimationClipLoader>());

    std::string good_path = write_file("clip_loader", "good.yaml",
        "clip:\n"
        "  name: test_clip\n"
        "  wrap: once\n"
        "  tracks:\n"
        "    - object: \"\"\n"
        "      component: Transform\n"
        "      property: position\n"
        "      keys:\n"
        "        - { time: 0.0, value: { x: 0, y: 0, z: 0 } }\n"
        "        - { time: 1.0, value: { x: 10, y: 0, z: 0 } }\n");
    auto good = assets.load<AnimationClip>(good_path);
    ASSERT_TRUE(good.is_loaded());
    ASSERT_EQ(good->name, std::string("test_clip"));
    ASSERT_EQ(good->tracks.size(), static_cast<size_t>(1));
    ASSERT_TRUE(std::abs(good->tracks[0].curve.keys()[1].time - 1.0f) < 1e-5f);
    ASSERT_TRUE(good->tracks[0].curve.keys()[1].easing == Interpolation::Linear);

    std::string bad_path = write_file("clip_loader", "bad.yaml",
        "not_a_clip: true\n");
    auto bad = assets.load<AnimationClip>(bad_path);
    ASSERT_TRUE(bad.is_failed());
    ASSERT_TRUE(!bad.error().empty());

    // No explicit shutdown() -- see test_asset_manager_sync_load_and_cache's
    // comment; good/bad are still live handles here.
}

void test_animator_scene_yaml_registration() {
    using namespace animation_test;
    using namespace coopa::anim;
    using namespace coopa::scene;

    coopa::asset::AssetManager assets;
    std::string clip_path = write_file("scene_yaml", "clip.yaml",
        "clip:\n"
        "  name: yaml_clip\n"
        "  wrap: once\n"
        "  tracks:\n"
        "    - object: \"\"\n"
        "      component: Transform\n"
        "      property: position\n"
        "      keys:\n"
        "        - { time: 0.0, value: { x: 0, y: 0, z: 0 } }\n"
        "        - { time: 1.0, value: { x: 10, y: 0, z: 0 } }\n");
    write_file("scene_yaml", "scene.yaml",
        "scene:\n"
        "  scene_name: YamlAnimatorScene\n"
        "  root_objects:\n"
        "    - name: Obj\n"
        "      components:\n"
        "        - type: Animator\n"
        "          auto_play: move\n"
        "          states:\n"
        "            - name: move\n"
        "              clip: clip.yaml\n");
    std::string scene_path = std::filesystem::path(clip_path).parent_path() / "scene.yaml";

    assets.add_search_root(std::filesystem::path(clip_path).parent_path().string());
    register_animation_components(assets);

    {
        // Scoped so `scene` (whose Animator holds an AssetHandle<AnimationClip>
        // bound to `assets`) is destroyed BEFORE assets.shutdown() runs below
        // -- shutdown() destroys every AssetSlot (see its doc), and that
        // handle's destructor dereferences the slot unconditionally.
        Scene scene = SceneLoader::load(scene_path);
        install_animation_system(scene);

        auto* obj = scene.find_object("Obj");
        ASSERT_TRUE(obj != nullptr);
        auto* animator = obj->get_component<Animator>();
        ASSERT_TRUE(animator != nullptr);

        // The clip loads synchronously fast enough in practice, but poll
        // briefly to avoid a flaky race against the asset IO thread rather
        // than assuming a fixed number of frames completes it.
        for (int i = 0; i < 200 && animator->binding_count() == 0; ++i) {
            assets.update(0.016f);
            scene.update(0.0f);
            scene.late_update(0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(animator->current_state(), std::string("move"));
        ASSERT_TRUE(animator->binding_count() > 0);

        scene.update(0.5f);
        scene.late_update(0.5f);
        auto pos = obj->get_transform()->transform().position();
        ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-2f);
    }

    // The registered parser's closure captures `assets` by reference --
    // clear it before `assets` is destroyed, mirroring every other
    // AssetManager-backed parser registration in this codebase.
    SceneLoader::clear_component_parsers();
    assets.shutdown();
}

void test_animation_system_parallel_matches_serial() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    constexpr int kAnimatorCount = 64;

    auto build_scene = [](Scene& scene) {
        for (int i = 0; i < kAnimatorCount; ++i) {
            auto obj = std::make_unique<SceneObject>("Obj" + std::to_string(i));
            obj->add_component<TransformComponent>();
            auto* animator = obj->add_component<Animator>();
            auto clip = std::make_shared<AnimationClip>();
            clip->wrap = WrapMode::Loop;
            clip->set_explicit_length(2.0f);
            AnimationTrack t;
            t.property = "position";
            t.curve.add_key(Keyframe{0.0f, {static_cast<float>(i), 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
            t.curve.add_key(Keyframe{2.0f, {static_cast<float>(i) * 2.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
            t.curve.sort_keys();
            clip->tracks.push_back(t);
            animator->add_state("s", clip);
            animator->auto_play = "s";
            scene.add_root_object(std::move(obj));
        }
    };

    Scene serial_scene("Serial");
    build_scene(serial_scene);
    AnimationSystem* serial_sys = install_animation_system(serial_scene);
    serial_sys->set_parallel_threshold(SIZE_MAX); // force serial
    serial_scene.start();
    serial_scene.update(0.37f);
    serial_scene.late_update(0.37f);

    coopa::job::JobEngine engine(4);
    Scene parallel_scene("Parallel");
    build_scene(parallel_scene);
    parallel_scene.set_job_engine(&engine);
    AnimationSystem* parallel_sys = install_animation_system(parallel_scene);
    parallel_sys->set_parallel_threshold(0); // force parallel
    parallel_sys->set_chunk_size(3);
    parallel_scene.start();
    parallel_scene.update(0.37f);
    parallel_scene.late_update(0.37f);

    for (int i = 0; i < kAnimatorCount; ++i) {
        std::string name = "Obj" + std::to_string(i);
        float serial_x = serial_scene.find_object(name)->get_transform()->transform().position().x;
        float parallel_x = parallel_scene.find_object(name)->get_transform()->transform().position().x;
        ASSERT_TRUE(std::abs(serial_x - parallel_x) < 1e-6f);
    }
    engine.shutdown();
}

void test_procedural_orbit_matches_legacy_formula() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("ProceduralOrbit");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip = std::make_shared<AnimationClip>();
    clip->wrap = WrapMode::Loop;
    clip->set_explicit_length(6.283185307f);
    AnimationTrack track;
    track.property = "position";
    track.kind = TrackKind::Procedural;
    track.procedural.type = "orbit";
    track.procedural.params.vectors["center"] = glm::vec4(-1.5f, 0.0f, 0.0f, 1.0f);
    track.procedural.params.scalars["radius"] = 2.7f;
    track.procedural.params.scalars["speed"] = 1.0f;
    track.procedural.params.scalars["height"] = 1.0f;
    track.procedural.params.scalars["initial_angle"] = 0.0f;
    clip->tracks.push_back(track);
    animator->add_state("orbit", clip);
    animator->auto_play = "orbit";

    install_animation_system(scene);
    scene.start();

    float times[] = {0.0f, 0.5f, 1.0f, 1.5707963f, 3.14159265f, 6.0f};
    float accumulated = 0.0f;
    for (float t : times) {
        float dt = t - accumulated;
        accumulated = t;
        scene.update(dt);
        scene.late_update(dt);
        // The old AnimationComponent's formula, verbatim -- this test is the
        // automated half of the blendy migration's correctness guarantee.
        float ex = -1.5f + 2.7f * std::cos(1.0f * t + 0.0f);
        float ey = 0.0f + 2.7f * std::sin(1.0f * t + 0.0f);
        float ez = 0.0f + 1.0f;
        auto pos = scene.find_object("Obj")->get_transform()->transform().position();
        ASSERT_TRUE(std::abs(pos.x - ex) < 1e-4f);
        ASSERT_TRUE(std::abs(pos.y - ey) < 1e-4f);
        ASSERT_TRUE(std::abs(pos.z - ez) < 1e-4f);
    }
}

void test_procedural_crossfade_against_keyframed() {
    using namespace coopa::anim;
    using namespace coopa::scene;

    Scene scene("ProceduralCrossfade");
    auto obj = std::make_unique<SceneObject>("Obj");
    obj->add_component<TransformComponent>();
    auto* animator = obj->add_component<Animator>();
    scene.add_root_object(std::move(obj));

    auto clip_a = std::make_shared<AnimationClip>(); // procedural constant at x=0
    clip_a->wrap = WrapMode::Loop;
    clip_a->set_explicit_length(10.0f);
    AnimationTrack ta;
    ta.property = "position";
    ta.kind = TrackKind::Procedural;
    ta.procedural.type = "constant";
    ta.procedural.params.vectors["value"] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    clip_a->tracks.push_back(ta);

    auto clip_b = std::make_shared<AnimationClip>(); // keyframed constant at x=10
    clip_b->wrap = WrapMode::Loop;
    clip_b->set_explicit_length(10.0f);
    AnimationTrack tb;
    tb.property = "position";
    tb.curve.add_key(Keyframe{0.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tb.curve.add_key(Keyframe{10.0f, {10.0f, 0.0f, 0.0f, 1.0f}, Interpolation::Linear});
    tb.curve.sort_keys();
    clip_b->tracks.push_back(tb);

    animator->add_state("A", clip_a);
    animator->add_state("B", clip_b);
    animator->auto_play = "A";

    install_animation_system(scene);
    scene.start();
    animator->crossfade("B", 1.0f);

    scene.update(0.5f);
    scene.late_update(0.5f); // 50% faded -- proves procedural and keyframed share the accumulator path
    auto pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 5.0f) < 1e-3f);

    scene.update(0.6f);
    scene.late_update(0.6f);
    pos = scene.find_object("Obj")->get_transform()->transform().position();
    ASSERT_TRUE(std::abs(pos.x - 10.0f) < 1e-3f);
}

// ---------------------------------------------------------
// coopa::input tests
// ---------------------------------------------------------
namespace input_test {

// --- input_map.h --- (ported from gfxcoopa/pixengine, KeyState-predicate form)

static void test_input_map_action_down_with_any_bound_key() {
    using namespace coopa::input;
    InputMap map;
    map.bind("jump", Key::Space);
    map.bind("jump", Key::W);

    // A magic keycode (e.g. a raw backend int) would have silently misbehaved
    // here under the sealed dense Key enum -- this test exists specifically
    // to keep that class of bug (see pixengine's original migration notes)
    // impossible.
    auto only_w_down = [](Key k) { return k == Key::W; };
    ASSERT_TRUE(map.is_down("jump", only_w_down));

    auto nothing_down = [](Key) { return false; };
    ASSERT_TRUE(!map.is_down("jump", nothing_down));
}

static void test_input_map_unbound_action_never_down() {
    using namespace coopa::input;
    InputMap map;
    auto always_true = [](Key) { return true; };
    ASSERT_TRUE(!map.is_down("nonexistent", always_true));
    ASSERT_TRUE(map.bindings("nonexistent").empty());
}

static void test_input_map_unbind_clears_bindings() {
    using namespace coopa::input;
    InputMap map;
    map.bind("fire", Key::F1);
    ASSERT_EQ(map.bindings("fire").size(), 1u);
    map.unbind("fire");
    ASSERT_TRUE(map.bindings("fire").empty());
    auto always_true = [](Key) { return true; };
    ASSERT_TRUE(!map.is_down("fire", always_true));
}

// --- input_map.h --- (new: mouse buttons, chords, axis/vector, against a real Input)

static void test_input_map_chord_requires_modifier() {
    using namespace coopa::input;
    InputMap map;
    map.bind("save", Key::S, Mods::Control);

    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::S, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(!map.is_down("save", input)); // S alone, no Control -- must not fire

    input.begin_frame(0.016f);
    input.push_key(Key::S, 0, KeyAction::Press, Mods::Control);
    ASSERT_TRUE(map.is_down("save", input)); // S + Control -- fires
}

static void test_input_map_mouse_button_binding() {
    using namespace coopa::input;
    InputMap map;
    map.bind("attack", MouseButton::Left);

    Input input;
    input.begin_frame(0.016f);
    ASSERT_TRUE(!map.is_down("attack", input));
    input.push_mouse_button(MouseButton::Left, KeyAction::Press, Mods::None);
    ASSERT_TRUE(map.is_down("attack", input));
    ASSERT_TRUE(map.is_pressed("attack", input));
}

static void test_input_map_axis_and_vector() {
    using namespace coopa::input;
    InputMap map;
    map.bind_axis("move_x", Key::D, Key::A);
    map.bind_vector("move", Key::D, Key::A, Key::W, Key::S);

    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::D, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(std::abs(map.axis("move_x", input) - 1.0f) < 1e-6f);
    glm::vec2 v = map.vector("move", input);
    ASSERT_TRUE(std::abs(v.x - 1.0f) < 1e-6f && std::abs(v.y - 0.0f) < 1e-6f);

    input.push_key(Key::W, 0, KeyAction::Press, Mods::None);
    v = map.vector("move", input);
    ASSERT_TRUE(std::abs(v.x - 1.0f) < 1e-6f && std::abs(v.y - 1.0f) < 1e-6f);

    // Both keys of an axis held -- they cancel to 0, not undefined behavior.
    input.push_key(Key::A, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(std::abs(map.axis("move_x", input) - 0.0f) < 1e-6f);
}

// --- input.h ---

static void test_input_press_release_edges_clear_on_begin_frame() {
    using namespace coopa::input;
    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::A, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(input.key_pressed(Key::A));
    ASSERT_TRUE(input.key_down(Key::A));

    // Next frame, no new events -- the press edge is gone, but "down" persists.
    input.begin_frame(0.016f);
    ASSERT_TRUE(!input.key_pressed(Key::A));
    ASSERT_TRUE(input.key_down(Key::A));

    input.push_key(Key::A, 0, KeyAction::Release, Mods::None);
    ASSERT_TRUE(input.key_released(Key::A));
    ASSERT_TRUE(!input.key_down(Key::A));
}

static void test_input_held_time_accumulates() {
    using namespace coopa::input;
    Input input;
    input.begin_frame(0.0f);
    input.push_key(Key::W, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(std::abs(input.key_held_time(Key::W) - 0.0f) < 1e-6f);

    input.begin_frame(0.5f); // W still down -- accumulates the elapsed 0.5s
    ASSERT_TRUE(std::abs(input.key_held_time(Key::W) - 0.5f) < 1e-6f);

    input.begin_frame(0.25f);
    ASSERT_TRUE(std::abs(input.key_held_time(Key::W) - 0.75f) < 1e-6f);

    input.push_key(Key::W, 0, KeyAction::Release, Mods::None);
    ASSERT_TRUE(std::abs(input.key_held_time(Key::W) - 0.0f) < 1e-6f);
}

static void test_input_press_and_release_within_one_frame_sets_both_edges() {
    using namespace coopa::input;
    Input input;
    input.begin_frame(0.016f);
    // Event-driven (unlike level-triggered polling), so a click-and-release
    // faster than one frame still registers both edges -- polling would miss it.
    input.push_mouse_button(MouseButton::Left, KeyAction::Press, Mods::None);
    input.push_mouse_button(MouseButton::Left, KeyAction::Release, Mods::None);
    ASSERT_TRUE(input.button_pressed(MouseButton::Left));
    ASSERT_TRUE(input.button_released(MouseButton::Left));
    ASSERT_TRUE(!input.button_down(MouseButton::Left));
}

static void test_input_cursor_delta_first_frame_and_after_mode_change() {
    using namespace coopa::input;
    Input input;
    input.begin_frame(0.016f);
    input.push_cursor_position(100.0, 100.0); // first ever -- suppressed to zero delta
    ASSERT_TRUE(input.cursor_delta() == glm::vec2(0.0f));

    input.begin_frame(0.016f);
    input.push_cursor_position(110.0, 105.0);
    ASSERT_TRUE(std::abs(input.cursor_delta().x - 10.0f) < 1e-6f);
    ASSERT_TRUE(std::abs(input.cursor_delta().y - 5.0f) < 1e-6f);

    // A mode change re-arms suppression -- the backend's next reported
    // position may jump arbitrarily (e.g. entering CursorMode::Disabled).
    input.set_cursor_mode(CursorMode::Disabled);
    input.begin_frame(0.016f);
    input.push_cursor_position(500.0, 500.0);
    ASSERT_TRUE(input.cursor_delta() == glm::vec2(0.0f));

    input.begin_frame(0.016f);
    input.push_cursor_position(505.0, 502.0);
    ASSERT_TRUE(std::abs(input.cursor_delta().x - 5.0f) < 1e-6f);
    ASSERT_TRUE(std::abs(input.cursor_delta().y - 2.0f) < 1e-6f);
}

static void test_input_release_all_on_focus_loss() {
    using namespace coopa::input;
    Input input;
    input.begin_frame(0.016f);
    input.push_key(Key::W, 0, KeyAction::Press, Mods::None);
    ASSERT_TRUE(input.key_down(Key::W));

    // The OS is not guaranteed to deliver W's release once focus is lost
    // (e.g. alt-tabbing away while holding it) -- push_focus(false) must
    // release it anyway, or it would read as stuck down indefinitely.
    input.push_focus(false);
    ASSERT_TRUE(!input.key_down(Key::W));
    ASSERT_TRUE(input.key_released(Key::W));
    ASSERT_TRUE(!input.focused());
}

} // namespace input_test

namespace item_test {

using namespace coopa::item;

// --- item_id.h ---

static void test_item_id_normalizes_and_hashes() {
    ItemId a = ItemId::from_name("Potion_Health");
    ItemId b = ItemId::from_name("  potion_health  ");
    ASSERT_TRUE(a == b);
    ASSERT_EQ(a.hash(), b.hash());
    ASSERT_TRUE(a.is_valid());
    ASSERT_EQ(a.str(), std::string("potion_health"));

    ItemId invalid;
    ASSERT_TRUE(!invalid.is_valid());

    std::unordered_map<ItemId, int> map;
    map[a] = 5;
    ASSERT_EQ(map[b], 5); // same normalized identity as a key
}

// --- item_database.h ---

static void test_item_database_define_find_clear() {
    ItemDatabase db;
    ItemDef def;
    def.id = ItemId::from_name("sword_iron");
    def.name = "Iron Sword";
    def.max_stack = 1;
    db.define(def);

    ASSERT_TRUE(db.contains(def.id));
    ASSERT_EQ(db.size(), 1u);
    const ItemDef* found = db.find(def.id);
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->name, "Iron Sword");
    ASSERT_EQ(db.max_stack_of(def.id), 1);

    ItemId unknown = ItemId::from_name("unknown_item");
    ASSERT_TRUE(db.find(unknown) == nullptr);
    ASSERT_EQ(db.max_stack_of(unknown), k_default_max_stack);

    db.clear();
    ASSERT_EQ(db.size(), 0u);
}

static ItemDatabase make_test_db() {
    ItemDatabase db;
    ItemDef potion;
    potion.id = ItemId::from_name("potion_health");
    potion.max_stack = 16;
    db.define(potion);

    ItemDef gold;
    gold.id = ItemId::from_name("coin_gold");
    gold.max_stack = 999;
    db.define(gold);

    ItemDef sword;
    sword.id = ItemId::from_name("sword_iron");
    sword.max_stack = 1;
    db.define(sword);

    ItemDef ruby;
    ruby.id = ItemId::from_name("gem_ruby");
    ruby.max_stack = 10;
    db.define(ruby);
    return db;
}

// --- inventory.h ---

static void test_inventory_add_fills_partial_stacks_then_empties() {
    ItemDatabase db = make_test_db();
    Inventory inv(2, &db);
    ItemId potion = ItemId::from_name("potion_health");

    inv.set(0, ItemStack{potion, 10}, false);
    int leftover = inv.add(potion, 20);

    // slot0 (10/16) fills to 16 (uses 6 of the 20), the remaining 14 spills
    // into the empty slot1.
    ASSERT_EQ(leftover, 0);
    ASSERT_EQ(inv.at(0).count, 16);
    ASSERT_TRUE(inv.at(1).item == potion);
    ASSERT_EQ(inv.at(1).count, 14);
}

static void test_inventory_add_returns_leftover_when_full() {
    ItemDatabase db = make_test_db();
    Inventory inv(1, &db);
    ItemId potion = ItemId::from_name("potion_health");
    inv.set(0, ItemStack{potion, 16}, false); // already at max_stack

    int leftover = inv.add(potion, 5);
    ASSERT_EQ(leftover, 5);
    ASSERT_EQ(inv.at(0).count, 16);
}

static void test_inventory_remove_across_slots_and_count_of() {
    ItemDatabase db = make_test_db();
    Inventory inv(3, &db);
    ItemId potion = ItemId::from_name("potion_health");
    inv.set(0, ItemStack{potion, 5}, false);
    inv.set(2, ItemStack{potion, 8}, false);

    ASSERT_EQ(inv.count_of(potion), 13);

    int removed = inv.remove(potion, 10);
    ASSERT_EQ(removed, 10);
    ASSERT_TRUE(inv.at(0).empty());
    ASSERT_EQ(inv.at(2).count, 3);
    ASSERT_EQ(inv.count_of(potion), 3);
}

static void test_inventory_move_or_merge_three_way() {
    ItemDatabase db = make_test_db();
    ItemId potion = ItemId::from_name("potion_health");
    ItemId sword  = ItemId::from_name("sword_iron");

    // Move into an empty slot.
    {
        Inventory inv(2, &db);
        inv.set(0, ItemStack{sword, 1}, false);
        ASSERT_TRUE(inv.move_or_merge(0, 1));
        ASSERT_TRUE(inv.at(0).empty());
        ASSERT_TRUE(inv.at(1).item == sword);
        ASSERT_EQ(inv.at(1).count, 1);
    }

    // Merge stacks of the same item.
    {
        Inventory inv(2, &db);
        inv.set(0, ItemStack{potion, 5}, false);
        inv.set(1, ItemStack{potion, 3}, false);
        ASSERT_TRUE(inv.move_or_merge(0, 1));
        ASSERT_TRUE(inv.at(0).empty());
        ASSERT_EQ(inv.at(1).count, 8);
    }

    // Swap two different items.
    {
        Inventory inv(2, &db);
        inv.set(0, ItemStack{potion, 4}, false);
        inv.set(1, ItemStack{sword, 1}, false);
        ASSERT_TRUE(inv.move_or_merge(0, 1));
        ASSERT_TRUE(inv.at(0).item == sword);
        ASSERT_TRUE(inv.at(1).item == potion);
        ASSERT_EQ(inv.at(1).count, 4);
    }
}

static void test_inventory_move_or_merge_partial_merge_leaves_remainder() {
    ItemDatabase db = make_test_db();
    ItemId potion = ItemId::from_name("potion_health"); // max_stack 16
    Inventory inv(2, &db);
    inv.set(0, ItemStack{potion, 5}, false);
    inv.set(1, ItemStack{potion, 14}, false);

    ASSERT_TRUE(inv.move_or_merge(0, 1));
    ASSERT_EQ(inv.at(1).count, 16);       // filled to max_stack
    ASSERT_TRUE(!inv.at(0).empty());
    ASSERT_EQ(inv.at(0).count, 3);        // remainder stays behind
}

static void test_inventory_move_or_merge_uses_itemdef_max_stack_not_stack_field() {
    // ItemStack itself carries no max_stack -- the ItemDatabase (via ItemId)
    // is the only authority. Two inventories sharing the same item id but
    // pointed at databases with different max_stack values must cap the
    // merge differently, proving the cap comes from the database, not from
    // any value cached alongside the stack.
    ItemId ruby = ItemId::from_name("gem_ruby");

    ItemDatabase small_db;
    ItemDef small_def; small_def.id = ruby; small_def.max_stack = 10;
    small_db.define(small_def);

    Inventory inv(2, &small_db);
    inv.set(0, ItemStack{ruby, 8}, false);
    inv.set(1, ItemStack{ruby, 8}, false);
    ASSERT_TRUE(inv.move_or_merge(0, 1));
    ASSERT_EQ(inv.at(1).count, 10);  // capped at small_db's max_stack
    ASSERT_EQ(inv.at(0).count, 6);   // 8 - (10-8) leftover in source
}

static void test_inventory_unknown_item_falls_back_to_default_max_stack() {
    Inventory inv(1, nullptr); // no database at all
    ItemId mystery = ItemId::from_name("mystery_item");

    int leftover = inv.add(mystery, 150);
    ASSERT_EQ(inv.at(0).count, k_default_max_stack);
    ASSERT_EQ(leftover, 150 - k_default_max_stack);
}

static void test_inventory_signals_fire_once_per_changed_slot() {
    ItemDatabase db = make_test_db();
    ItemId potion = ItemId::from_name("potion_health");
    Inventory inv(2, &db);
    inv.set(0, ItemStack{potion, 5}, false);
    inv.set(1, ItemStack{potion, 3}, false);

    int changed_count = 0;
    int swapped_count = 0;
    auto c1 = inv.on_slot_changed.connect([&](int, const ItemStack&) { changed_count++; });
    auto c2 = inv.on_slots_swapped.connect([&](int, int) { swapped_count++; });

    ASSERT_TRUE(inv.move_or_merge(0, 1)); // merge -- both slots change, one swap notification
    ASSERT_EQ(changed_count, 2);
    ASSERT_EQ(swapped_count, 1);
}

static void test_inventory_split_and_clear_slot() {
    ItemDatabase db = make_test_db();
    ItemId potion = ItemId::from_name("potion_health");
    Inventory inv(2, &db);
    inv.set(0, ItemStack{potion, 10}, false);

    ASSERT_TRUE(inv.split(0, 4, 1));
    ASSERT_EQ(inv.at(0).count, 6);
    ASSERT_EQ(inv.at(1).count, 4);

    ASSERT_TRUE(!inv.split(0, 0, 1)); // count must be > 0 -- slot1 no longer empty anyway

    inv.clear_slot(0);
    ASSERT_TRUE(inv.at(0).empty());
}

// --- hotbar.h ---

static void test_hotbar_select_wraps_and_emits_only_on_change() {
    Inventory inv(3);
    Hotbar hb(&inv, 0, 3);

    int emit_count = 0;
    auto conn = hb.on_selection_changed.connect([&](int) { emit_count++; });

    hb.select(0);
    ASSERT_EQ(hb.selected(), 0);
    ASSERT_EQ(emit_count, 1);

    hb.select(0); // no change -- no emit
    ASSERT_EQ(emit_count, 1);

    hb.next();
    ASSERT_EQ(hb.selected(), 1);
    ASSERT_EQ(emit_count, 2);

    hb.select(2);
    hb.next(); // wraps past the end back to 0
    ASSERT_EQ(hb.selected(), 0);

    hb.prev(); // wraps before the start to count()-1
    ASSERT_EQ(hb.selected(), 2);

    hb.select(-1);
    ASSERT_EQ(hb.selected(), -1);
    ASSERT_EQ(hb.selected_slot(), -1);
}

static void test_hotbar_selected_stack_reads_through_to_inventory() {
    ItemDatabase db = make_test_db();
    ItemId potion = ItemId::from_name("potion_health");
    Inventory inv(3, &db);
    inv.set(1, ItemStack{potion, 5}, false);

    Hotbar hb(&inv, 0, 3);
    hb.select(1);
    ASSERT_TRUE(hb.selected_stack().item == potion);
    ASSERT_EQ(hb.selected_stack().count, 5);

    hb.select(-1);
    ASSERT_TRUE(hb.selected_stack().empty());
}

// --- item_database_loader.h ---

static void test_item_database_loader_parses_yaml() {
    std::string yaml =
        "items:\n"
        "  - id: potion_health\n"
        "    name: Health Potion\n"
        "    description: Restores health.\n"
        "    icon: potion\n"
        "    max_stack: 16\n"
        "    category: consumable\n"
        "    rarity: common\n"
        "    tint: { r: 0.9, g: 0.2, b: 0.3, a: 0.95 }\n"
        "  - id: mystery_box\n";
    fkyaml::node root = fkyaml::node::deserialize(yaml);
    ItemDatabase db;
    parse_item_database(root, db);
    ASSERT_EQ(db.size(), 2u);

    const ItemDef* potion = db.find(ItemId::from_name("potion_health"));
    ASSERT_TRUE(potion != nullptr);
    ASSERT_EQ(potion->name, "Health Potion");
    ASSERT_EQ(potion->max_stack, 16);
    ASSERT_TRUE(potion->category == ItemCategory::Consumable);
    ASSERT_TRUE(potion->rarity == ItemRarity::Common);
    ASSERT_TRUE(std::abs(potion->tint.a - 0.95f) < 1e-5f);

    // Fields omitted entirely fall back to ItemDef's own defaults.
    const ItemDef* mystery = db.find(ItemId::from_name("mystery_box"));
    ASSERT_TRUE(mystery != nullptr);
    ASSERT_EQ(mystery->max_stack, k_default_max_stack);
    ASSERT_TRUE(mystery->category == ItemCategory::Misc);
    ASSERT_TRUE(mystery->rarity == ItemRarity::Common);

    // Unrecognized category/rarity strings degrade rather than throw.
    std::string yaml2 = "items:\n  - id: weird_item\n    category: nonsense\n    rarity: bogus\n";
    fkyaml::node root2 = fkyaml::node::deserialize(yaml2);
    ItemDatabase db2;
    parse_item_database(root2, db2);
    const ItemDef* weird = db2.find(ItemId::from_name("weird_item"));
    ASSERT_TRUE(weird != nullptr);
    ASSERT_TRUE(weird->category == ItemCategory::Misc);
    ASSERT_TRUE(weird->rarity == ItemRarity::Common);
}

static void test_item_database_loader_merges_into_existing() {
    ItemDatabase db;
    ItemDef existing;
    existing.id = ItemId::from_name("coin_gold");
    existing.max_stack = 999;
    db.define(existing);

    std::string yaml = "items:\n  - id: potion_mana\n    max_stack: 8\n";
    fkyaml::node root = fkyaml::node::deserialize(yaml);
    parse_item_database(root, db);

    ASSERT_EQ(db.size(), 2u);
    ASSERT_TRUE(db.contains(ItemId::from_name("coin_gold")));
    ASSERT_TRUE(db.contains(ItemId::from_name("potion_mana")));
}

} // namespace item_test

namespace stat_test {

using namespace coopa::stat;

static void test_resource_damage_heal_clamped() {
    Resource r(100.0f);
    r.damage(30.0f);
    ASSERT_TRUE(std::abs(r.current - 70.0f) < 1e-4f);

    r.damage(1000.0f);
    ASSERT_TRUE(r.current == 0.0f);
    ASSERT_TRUE(r.is_depleted());

    r.heal(1000.0f);
    ASSERT_TRUE(r.current == r.max);
    ASSERT_TRUE(r.is_full());
}

static void test_resource_set_max_keep_ratio_and_clamp() {
    Resource r(100.0f);
    r.damage(50.0f); // current = 50, normalized 0.5
    r.set_max(50.0f, /*keep_ratio=*/true);
    ASSERT_TRUE(std::abs(r.max - 50.0f) < 1e-4f);
    ASSERT_TRUE(std::abs(r.current - 25.0f) < 1e-4f); // 0.5 * 50

    Resource r2(100.0f);
    r2.damage(10.0f); // current = 90
    r2.set_max(50.0f, /*keep_ratio=*/false);
    ASSERT_TRUE(std::abs(r2.current - 50.0f) < 1e-4f); // clamped down to the new max
}

static void test_resource_tick_regenerates_after_delay() {
    Resource r(100.0f, /*regen=*/10.0f, /*delay=*/1.0f);
    r.damage(50.0f);

    r.tick(0.5f); // still inside the post-damage delay window
    ASSERT_TRUE(std::abs(r.current - 50.0f) < 1e-4f);

    r.tick(0.6f); // crosses the delay -- regen resumes
    ASSERT_TRUE(r.current > 50.0f);
    float after_first_regen_tick = r.current;

    r.tick(0.5f);
    ASSERT_TRUE(r.current > after_first_regen_tick);

    r.tick(1000.0f); // a long tick clamps at max, doesn't overshoot
    ASSERT_TRUE(r.is_full());
}

static void test_resource_on_depleted_fires_once_at_zero() {
    Resource r(100.0f);
    int depleted_count = 0;
    auto conn = r.on_depleted.connect([&]() { depleted_count++; });

    r.damage(50.0f);
    ASSERT_EQ(depleted_count, 0);

    r.damage(50.0f); // crosses to 0
    ASSERT_EQ(depleted_count, 1);

    r.damage(10.0f); // already at 0 -- no further edge
    ASSERT_EQ(depleted_count, 1);

    r.heal(20.0f);
    r.damage(20.0f); // crosses to 0 again -- a fresh edge
    ASSERT_EQ(depleted_count, 2);
}

static void test_resource_on_changed_silent_when_unchanged() {
    Resource r(100.0f); // regen_per_second = 0 by default
    int changed_count = 0;
    auto conn = r.on_changed.connect([&](float, float) { changed_count++; });

    r.tick(1.0f);        // already full, no regen configured -- silent
    ASSERT_EQ(changed_count, 0);

    r.damage(0.0f);       // non-positive amount -- no-op
    ASSERT_EQ(changed_count, 0);

    r.damage(10.0f);      // a real change
    ASSERT_EQ(changed_count, 1);
}

static void test_stat_block_named_resources_are_pointer_stable() {
    StatBlock block;
    Resource* health = &block.resource("health");
    health->current = 42.0f;

    for (int i = 0; i < 50; ++i) {
        block.resource("stat_" + std::to_string(i));
    }

    ASSERT_TRUE(std::abs(health->current - 42.0f) < 1e-4f);
    ASSERT_EQ(block.size(), 51u);
}

} // namespace stat_test

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "         Running libcoopa Test Suite       " << std::endl;
    std::cout << "===========================================" << std::endl;

    scene_inherit_test::register_components();
    animation_test::register_properties();

    RUN_TEST(test_id_util);
    RUN_TEST(test_string_util);
    RUN_TEST(test_math_util);
    RUN_TEST(test_file_util);
    RUN_TEST(test_yaml_map);
    RUN_TEST(test_parallel_queue);
    RUN_TEST(test_parallel_vector);
    RUN_TEST(test_parallel_map);
    RUN_TEST(test_job_engine);
    RUN_TEST(test_job_scheduler);
    RUN_TEST(test_job_scheduler_main_thread);
    RUN_TEST(test_job_engine_dependencies);
    RUN_TEST(test_job_engine_chained_dependencies);
    RUN_TEST(test_job_engine_fan_in_dependencies);
    RUN_TEST(test_job_engine_repeated_engines_no_lost_or_phantom_jobs);
    RUN_TEST(test_work_stealing_deque);
    RUN_TEST(test_work_stealing_deque_no_torn_moves_under_contention);
    RUN_TEST(test_job_handle_generation_prevents_stale_aliasing);
    RUN_TEST(test_job_engine_fan_in_16_dependencies);
    RUN_TEST(test_job_engine_deque_overflow_falls_back_to_global_queue);
    RUN_TEST(test_job_engine_counter_pool_exhaustion_recovers);
    RUN_TEST(test_job_engine_nested_wait_for_from_worker);
    RUN_TEST(test_job_engine_parallel_for_covers_range_exactly_once);
    RUN_TEST(test_job_engine_cancel_skips_body_but_releases_counter);
    RUN_TEST(test_job_engine_priority_ordering_single_worker);
    RUN_TEST(test_job_handle_survives_begin_end_frame);
    RUN_TEST(test_job_engine_shared_across_frames_and_subsystems);
    RUN_TEST(test_debug_logging);
    RUN_TEST(test_signal_basic_emit);
    RUN_TEST(test_signal_disconnect);
    RUN_TEST(test_signal_scoped_connection);
    RUN_TEST(test_signal_reentrancy_self_disconnect);
    RUN_TEST(test_signal_reentrancy_connect_during_emit);
    RUN_TEST(test_signal_disconnect_all_and_id_reuse);
    RUN_TEST(test_signal_connection_outlives_signal);
    RUN_TEST(test_signal_destroyed_from_slot);
    RUN_TEST(test_signal_reference_args_no_copy);
    RUN_TEST(test_asset_id);
    RUN_TEST(test_asset_source);
    RUN_TEST(test_asset_manager_sync_load_and_cache);
    RUN_TEST(test_asset_manager_rejects_type_mismatch);
    RUN_TEST(test_asset_manager_base_dir_prevents_collision);
    RUN_TEST(test_asset_manager_async_load);
    RUN_TEST(test_asset_manager_hot_reload);
    RUN_TEST(test_asset_manager_unload_and_gc);
    RUN_TEST(test_asset_manager_shutdown_drains_pending);
    RUN_TEST(test_asset_manager_create_publishes_runtime_payload);
    RUN_TEST(test_asset_manager_create_republishes_with_grace_period);
    RUN_TEST(test_asset_manager_idle_eviction_defers_payload_destruction);

    RUN_TEST(test_scene_inherit_object_from_file);
    RUN_TEST(test_scene_inherit_component_merge_by_type);
    RUN_TEST(test_scene_inherit_component_id_disambiguation);
    RUN_TEST(test_scene_inherit_children_merge_and_append);
    RUN_TEST(test_scene_inherit_remove);
    RUN_TEST(test_scene_inherit_scene_level);
    RUN_TEST(test_scene_inherit_chain);
    RUN_TEST(test_scene_inherit_multiple_bases);
    RUN_TEST(test_scene_inherit_cycle_throws);
    RUN_TEST(test_scene_inherit_missing_file_throws);
    RUN_TEST(test_scene_inherit_asset_path_resolution);
    RUN_TEST(test_scene_inherit_no_inherit_unchanged);

    RUN_TEST(test_scene_phase_order);
    RUN_TEST(test_scene_update_late_update_split);
    RUN_TEST(test_scene_late_update_standalone);
    RUN_TEST(test_scene_frame_boundary_update_only);
    RUN_TEST(test_scene_null_job_engine_runs_inline);
    RUN_TEST(test_scene_add_remove_system);
    RUN_TEST(test_scene_move_preserves_systems);

    RUN_TEST(test_scene_command_buffer_flushes_in_ascending_worker_index_order);
    RUN_TEST(test_scene_command_buffer_parallel_writes_all_land_exactly_once);
    RUN_TEST(test_scene_manager_concurrent_scenes_match_serial);
    RUN_TEST(test_scene_manager_no_engine_additive_active_remove);
    RUN_TEST(test_transform_set_rotation_euler_unaffected_by_quat_support);
    RUN_TEST(test_transform_set_rotation_quat_matches_mat4_cast);
    RUN_TEST(test_transform_system_matches_lazy_resolve);
    RUN_TEST(test_transform_system_concurrent_reads_are_race_free);

    RUN_TEST(test_animation_curve_sampling);
    RUN_TEST(test_animation_property_registry);
    RUN_TEST(test_animator_plays_clip_on_transform);
    RUN_TEST(test_animator_channel_mask_preserves_other_channels);
    RUN_TEST(test_animator_dedups_bindings_no_clobber);
    RUN_TEST(test_animator_wrap_modes);
    RUN_TEST(test_animator_crossfade_blends);
    RUN_TEST(test_animator_binds_by_name_and_path);
    RUN_TEST(test_animator_lazy_binds_when_initially_inactive);
    RUN_TEST(test_animation_rotation_does_not_wrap);
    RUN_TEST(test_animation_clip_loader_yaml);
    RUN_TEST(test_animation_system_parallel_matches_serial);
    RUN_TEST(test_procedural_orbit_matches_legacy_formula);
    RUN_TEST(test_procedural_crossfade_against_keyframed);
    // Run last: registers the "Animator" SceneLoader parser with a closure
    // capturing a local AssetManager, and tears both down at the end.
    RUN_TEST(test_animator_scene_yaml_registration);

    RUN_TEST(input_test::test_input_map_action_down_with_any_bound_key);
    RUN_TEST(input_test::test_input_map_unbound_action_never_down);
    RUN_TEST(input_test::test_input_map_unbind_clears_bindings);
    RUN_TEST(input_test::test_input_map_chord_requires_modifier);
    RUN_TEST(input_test::test_input_map_mouse_button_binding);
    RUN_TEST(input_test::test_input_map_axis_and_vector);
    RUN_TEST(input_test::test_input_press_release_edges_clear_on_begin_frame);
    RUN_TEST(input_test::test_input_held_time_accumulates);
    RUN_TEST(input_test::test_input_press_and_release_within_one_frame_sets_both_edges);
    RUN_TEST(input_test::test_input_cursor_delta_first_frame_and_after_mode_change);
    RUN_TEST(input_test::test_input_release_all_on_focus_loss);

    RUN_TEST(item_test::test_item_id_normalizes_and_hashes);
    RUN_TEST(item_test::test_item_database_define_find_clear);
    RUN_TEST(item_test::test_inventory_add_fills_partial_stacks_then_empties);
    RUN_TEST(item_test::test_inventory_add_returns_leftover_when_full);
    RUN_TEST(item_test::test_inventory_remove_across_slots_and_count_of);
    RUN_TEST(item_test::test_inventory_move_or_merge_three_way);
    RUN_TEST(item_test::test_inventory_move_or_merge_partial_merge_leaves_remainder);
    RUN_TEST(item_test::test_inventory_move_or_merge_uses_itemdef_max_stack_not_stack_field);
    RUN_TEST(item_test::test_inventory_unknown_item_falls_back_to_default_max_stack);
    RUN_TEST(item_test::test_inventory_signals_fire_once_per_changed_slot);
    RUN_TEST(item_test::test_inventory_split_and_clear_slot);
    RUN_TEST(item_test::test_hotbar_select_wraps_and_emits_only_on_change);
    RUN_TEST(item_test::test_hotbar_selected_stack_reads_through_to_inventory);
    RUN_TEST(item_test::test_item_database_loader_parses_yaml);
    RUN_TEST(item_test::test_item_database_loader_merges_into_existing);

    RUN_TEST(stat_test::test_resource_damage_heal_clamped);
    RUN_TEST(stat_test::test_resource_set_max_keep_ratio_and_clamp);
    RUN_TEST(stat_test::test_resource_tick_regenerates_after_delay);
    RUN_TEST(stat_test::test_resource_on_depleted_fires_once_at_zero);
    RUN_TEST(stat_test::test_resource_on_changed_silent_when_unchanged);
    RUN_TEST(stat_test::test_stat_block_named_resources_are_pointer_stable);

    std::cout << "===========================================" << std::endl;
    std::cout << "Test Summary: " << g_tests_run - g_tests_failed << " / " << g_tests_run << " Passed." << std::endl;
    if (g_tests_failed > 0) {
        std::cout << ANSI_COLOR_RED << "Some tests failed!" << ANSI_COLOR_RESET << std::endl;
        return 1;
    } else {
        std::cout << ANSI_COLOR_GREEN << "All tests passed successfully!" << ANSI_COLOR_RESET << std::endl;
        return 0;
    }
}
