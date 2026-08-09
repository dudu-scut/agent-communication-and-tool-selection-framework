#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include "agent_rpc/common/background_scheduler.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace agent_rpc::common;

// Test 1: basic schedule + execution
TEST(BackgroundSchedulerTest, SingleTaskExecutes) {
    auto& sched = BackgroundScheduler::instance();
    std::atomic<int> count{0};

    (void)sched.scheduleAtFixedRate("test_counter",
        [&]() { count.fetch_add(1); },
        std::chrono::milliseconds(100));
    sched.start(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    sched.stop();

    // With 100ms interval, expect at least 2-4 executions
    int c = count.load();
    EXPECT_GE(c, 2) << "Task should have fired at least 2 times in 350ms";
    EXPECT_LE(c, 8) << "Task shouldn't fire excessively";
}

// Test 2: reentrancy prevention — slow task doesn't get re-queued
TEST(BackgroundSchedulerTest, PreventsReentrancy) {
    auto& sched = BackgroundScheduler::instance();
    std::atomic<int> start_count{0};
    std::atomic<int> complete_count{0};
    std::atomic<bool> blocker{true};

    (void)sched.scheduleAtFixedRate("slow_task", [&]() {
        start_count.fetch_add(1);
        while (blocker.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        complete_count.fetch_add(1);
    }, std::chrono::milliseconds(50));

    sched.start(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Task should have started exactly once because running=true blocks re-dispatch
    EXPECT_EQ(start_count.load(), 1);
    EXPECT_EQ(complete_count.load(), 0);

    blocker.store(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sched.stop();

    EXPECT_GE(complete_count.load(), 1);
}

// Test 3: cancel stops execution
TEST(BackgroundSchedulerTest, CancelStopsExecution) {
    auto& sched = BackgroundScheduler::instance();
    std::atomic<int> count{0};

    int id = sched.scheduleAtFixedRate("cancel_test",
        [&]() { count.fetch_add(1); },
        std::chrono::milliseconds(50));
    sched.start(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    sched.cancel(id);
    int count_after_cancel = count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    sched.stop();

    // Count should not increase significantly after cancel
    EXPECT_LE(count.load(), count_after_cancel + 1);
}

// Property test: interval-based scheduling respects minimum spacing
RC_GTEST_PROP(BackgroundSchedulerProp, TaskDoesNotFireBeforeInterval,
              (const uint32_t interval_ms)) {
    constexpr uint32_t kMinIntervalMs = 10;
    constexpr uint32_t kIntervalRangeMs = 20;
    constexpr auto kObservationWindow = std::chrono::milliseconds(150);

    uint32_t interval = kMinIntervalMs + (interval_ms % kIntervalRangeMs); // 10-29ms
    auto& sched = BackgroundScheduler::instance();
    std::atomic<int> count{0};
    std::atomic<long long> first_fire_ms{-1};
    const auto scheduled_at = std::chrono::steady_clock::now();

    (void)sched.scheduleAtFixedRate("prop_test",
        [&]() {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scheduled_at).count();
            if (count.fetch_add(1) == 0) {
                first_fire_ms.store(elapsed);
            }
        },
        std::chrono::milliseconds(interval));
    sched.start(1);
    std::this_thread::sleep_for(kObservationWindow);
    sched.stop();

    RC_ASSERT(first_fire_ms.load() >= interval);
    RC_ASSERT(count.load() >= 2); // At least 2 fires during the observation window
}
