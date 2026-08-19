#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <cstdio> // For std::remove
#include <fstream>
#include <filesystem>

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
#include <coopa/job/thread.h>
#include <coopa/job/engine.h>
#include <coopa/job/scheduler.h>
#include <coopa/event/signal.h>
#include <coopa/asset/asset_manager.h>

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

    assets.shutdown();
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

    assets.shutdown();
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

    assets.shutdown();
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

    assets.shutdown();
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

    assets.shutdown();
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

    auto handle = assets.load_async<TextAsset>(test_file);
    ASSERT_TRUE(handle.is_valid());

    // Deliberately do not call update() first -- shutdown() itself must
    // wait for the in-flight decode() job and run finalize() before tearing
    // down pending_, rather than destroying that job's state out from under
    // a still-running worker thread.
    assets.shutdown();

    ASSERT_TRUE(*finalized);

    std::remove(test_file.c_str());
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "         Running libcoopa Test Suite       " << std::endl;
    std::cout << "===========================================" << std::endl;

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
