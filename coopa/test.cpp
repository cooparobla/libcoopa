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
    trav::collections::YAMLMap map;
    map.set<int>("some_int", 42);
    map.set<std::string>("some_str", "hello");
    ASSERT_EQ(map.get<int>("some_int", 0), 42);
    ASSERT_EQ(map.get<std::string>("some_str", ""), "hello");

    std::string test_yaml = "test_temp_config.yaml";
    map.save(test_yaml);

    trav::collections::YAMLMap map_loaded = trav::collections::YAMLMap::load(test_yaml);
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

    trav::collections::YAMLMap nested;
    nested.set<double>("pi", 3.14159);
    map_loaded.set<fkyaml::node>("nested", nested.get_raw_node());
    auto nested_loaded = map_loaded.get_node("nested");
    ASSERT_EQ(nested_loaded.get<double>("pi", 0.0), 3.14159);

    std::remove(test_yaml.c_str());
}

void test_parallel_queue() {
    trav::job::ParallelQueue<int> queue;
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
    trav::job::ParallelVector<std::string> pvec;
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
    trav::job::ParallelMap<std::string, int> pmap;
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
    trav::job::JobEngine engine(4);
    engine.begin_frame();
    
    std::atomic<int> counter{0};
    trav::job::JobHandle handle = engine.create_handle();
    
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
    trav::job::JobEngine engine(4);
    trav::job::JobScheduler scheduler(engine);

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
    trav::job::JobEngine engine(4);
    trav::job::JobScheduler scheduler(engine);

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
    trav::job::JobEngine engine(4);
    engine.begin_frame();

    std::atomic<int> value{0};

    // Job A: sets value to 10 after a short delay.
    trav::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        value.store(10, std::memory_order_release);
    }, 1, handle_a);

    // Job B: depends on A, multiplies value by 3.
    // If dependency works, B runs after A and value becomes 30.
    // If dependency is broken, B may run before A and value could be 0*3=0 or 10.
    trav::job::JobHandle handle_b = engine.create_handle();
    trav::job::JobHandle deps_b[] = { handle_a };
    engine.submit([&]() {
        int v = value.load(std::memory_order_acquire);
        value.store(v * 3, std::memory_order_release);
    }, 1, handle_b, deps_b, 1);

    engine.wait_for(handle_b);
    ASSERT_EQ(value.load(), 30);

    engine.end_frame();
}

void test_job_engine_chained_dependencies() {
    trav::job::JobEngine engine(4);
    engine.begin_frame();

    // Track execution order: A must run before B, B before C.
    std::atomic<int> sequence{0};
    std::atomic<bool> order_correct{true};

    // Job A: sets sequence to 1.
    trav::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int expected = 0;
        if (!sequence.compare_exchange_strong(expected, 1)) {
            order_correct.store(false);
        }
    }, 1, handle_a);

    // Job B: depends on A, sets sequence to 2.
    trav::job::JobHandle handle_b = engine.create_handle();
    trav::job::JobHandle deps_b[] = { handle_a };
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        int expected = 1;
        if (!sequence.compare_exchange_strong(expected, 2)) {
            order_correct.store(false);
        }
    }, 1, handle_b, deps_b, 1);

    // Job C: depends on B (transitively on A), sets sequence to 3.
    trav::job::JobHandle handle_c = engine.create_handle();
    trav::job::JobHandle deps_c[] = { handle_b };
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
    trav::job::JobEngine engine(4);
    engine.begin_frame();

    // Two independent jobs (A and B) must both complete before C runs.
    std::atomic<int> completed_count{0};
    std::atomic<bool> c_ran_after_both{false};

    // Job A: increments completed_count after delay.
    trav::job::JobHandle handle_a = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        completed_count.fetch_add(1, std::memory_order_release);
    }, 1, handle_a);

    // Job B: increments completed_count after different delay.
    trav::job::JobHandle handle_b = engine.create_handle();
    engine.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        completed_count.fetch_add(1, std::memory_order_release);
    }, 1, handle_b);

    // Job C: depends on BOTH A and B.
    trav::job::JobHandle handle_c = engine.create_handle();
    trav::job::JobHandle deps_c[] = { handle_a, handle_b };
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

void test_work_stealing_deque() {
    trav::job::WorkStealingDeque<int> deque(16);

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

void test_debug_logging() {
    trav::debug::Logger logger("TestLogger");
    logger.info("This is an info log");
    logger.warn("This is a warning log");
    logger.error("This is an error log");

    trav::debug::DebugManager manager;
    auto& ctx = manager.get_context();
    ctx.info("Message from context 1", "TAG_1");
    ctx.info("Message from context 2", "TAG_2");
    manager.show();

    trav::debug::DebugBucket bucket;
    auto& bctx = bucket.get_context();
    bctx.info("Bucket message 1", "BUCKET_TAG");
    bucket.show();
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
    RUN_TEST(test_work_stealing_deque);
    RUN_TEST(test_debug_logging);

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
