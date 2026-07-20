/**
 * @file test_circuit_breaker_edge.cpp
 * @brief Edge-case and boundary tests for CircuitBreaker state machine
 *
 * Risk analysis identified: silent failure patterns, state transition races,
 * unvalidated config values, division-by-zero risks with min_request_count=0,
 * and inconsistent snapshot issues between stats_ and state_.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>

#include "agent_rpc/common/circuit_breaker.h"

using namespace agent_rpc::common;
using namespace std::chrono_literals;

// ── Config Edge Cases ─────────────────────────────────────────────────────────

TEST(CircuitBreakerEdgeTest, ZeroFailureThreshold_OpensOnFirstFailure) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 0;
    cfg.min_request_count = 0;

    CircuitBreaker cb(cfg);
    EXPECT_TRUE(cb.isRequestAllowed());

    // With failure_threshold=0 and failure_rate_threshold=0.5,
    // the first failure should trigger transition to OPEN
    cb.recordFailure();
    // After recordFailure, updateFailureRate is called:
    // failure_rate = failed/total = 1/1 = 1.0 >= 0.5 threshold → transitionToOpen
    EXPECT_FALSE(cb.isRequestAllowed());
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);
}

TEST(CircuitBreakerEdgeTest, ZeroMinRequestCount_DivisionByZeroProtection) {
    CircuitBreakerConfig cfg;
    cfg.min_request_count = 0;
    cfg.failure_rate_threshold = 0.0;  // Any failure opens

    CircuitBreaker cb(cfg);
    EXPECT_TRUE(cb.isRequestAllowed());

    // With min_request_count=0, updateFailureRate should still not divide by zero
    // because the check is: total >= min_request_count (=0) → always true
    cb.recordFailure();
    EXPECT_FALSE(cb.isRequestAllowed());
}

TEST(CircuitBreakerEdgeTest, NegativeConfigValues_DoesNotCrash) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = -5;
    cfg.success_threshold = -1;
    cfg.min_request_count = -10;

    CircuitBreaker cb(cfg);
    // Should not crash during construction or operation
    EXPECT_TRUE(cb.isRequestAllowed());
    cb.recordFailure();
    cb.recordSuccess();
    EXPECT_NO_THROW(cb.getStats());
}

TEST(CircuitBreakerEdgeTest, ZeroTimeout_ImmediateHalfOpen) {
    CircuitBreakerConfig cfg;
    cfg.timeout = 0ms;
    cfg.failure_threshold = 1;

    CircuitBreaker cb(cfg);
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);

    // With zero timeout, shouldAttemptReset should immediately return true
    // So isRequestAllowed should transition to HALF_OPEN immediately
    EXPECT_TRUE(cb.isRequestAllowed());
    EXPECT_EQ(cb.getState(), CircuitState::HALF_OPEN);
}

// ── State Transition Edge Cases ───────────────────────────────────────────────

TEST(CircuitBreakerEdgeTest, FullCycle_CLOSED_TO_OPEN_TO_HALF_OPEN_TO_CLOSED) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 1;
    cfg.success_threshold = 2;
    cfg.timeout = 10ms;
    cfg.failure_rate_threshold = 0.0;  // Any failure → open (with min_request_count=0 → updateFailureRate always checks)

    CircuitBreaker cb(cfg);

    // CLOSED → record 1 failure → OPEN
    EXPECT_TRUE(cb.isRequestAllowed());
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);

    // Wait for timeout so reset is attempted
    std::this_thread::sleep_for(20ms);

    // OPEN → HALF_OPEN (via isRequestAllowed)
    EXPECT_TRUE(cb.isRequestAllowed());
    EXPECT_EQ(cb.getState(), CircuitState::HALF_OPEN);

    // HALF_OPEN → record 2 successes → CLOSED
    cb.recordSuccess();
    EXPECT_EQ(cb.getState(), CircuitState::HALF_OPEN);  // Still half-open after 1st success
    cb.recordSuccess();
    EXPECT_EQ(cb.getState(), CircuitState::CLOSED);       // Closed after 2nd success
}

TEST(CircuitBreakerEdgeTest, HalfOpen_FailureReturnsToOpen) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 1;
    cfg.success_threshold = 3;
    cfg.timeout = 10ms;

    CircuitBreaker cb(cfg);

    // CLOSED → OPEN
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);

    std::this_thread::sleep_for(20ms);

    // OPEN → HALF_OPEN
    EXPECT_TRUE(cb.isRequestAllowed());
    EXPECT_EQ(cb.getState(), CircuitState::HALF_OPEN);

    // Record failure in HALF_OPEN → back to OPEN
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);
}

TEST(CircuitBreakerEdgeTest, Reset_ClearsAllStats) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 5;

    CircuitBreaker cb(cfg);
    for (int i = 0; i < 3; i++) { cb.recordFailure(); }
    for (int i = 0; i < 2; i++) { cb.recordSuccess(); }

    auto stats_before = cb.getStats();
    EXPECT_EQ(stats_before.failed_requests, 3);
    EXPECT_EQ(stats_before.successful_requests, 2);
    EXPECT_EQ(stats_before.total_requests, 5);

    cb.reset();

    auto stats_after = cb.getStats();
    EXPECT_EQ(stats_after.total_requests, 0);
    EXPECT_EQ(stats_after.successful_requests, 0);
    EXPECT_EQ(stats_after.failed_requests, 0);
    EXPECT_EQ(stats_after.rejected_requests, 0);
    EXPECT_EQ(cb.getState(), CircuitState::CLOSED);
}

// ── Concurrency Tests ─────────────────────────────────────────────────────────

TEST(CircuitBreakerEdgeTest, ConcurrentRecordFailure_StatsConsistent) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 1000;  // Won't open during test
    cfg.min_request_count = 1000;

    CircuitBreaker cb(cfg);
    const int threads = 4;
    const int per_thread = 250;
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            while (!start.load()) { /* spin */ }
            for (int i = 0; i < per_thread; i++) {
                cb.recordFailure();
            }
        });
    }

    start.store(true);
    for (auto& w : workers) { w.join(); }

    auto stats = cb.getStats();
    EXPECT_EQ(stats.failed_requests, threads * per_thread);
    EXPECT_EQ(stats.total_requests, threads * per_thread);
}

TEST(CircuitBreakerEdgeTest, ConcurrentRecordSuccess_StatsConsistent) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 1000;
    cfg.min_request_count = 1000;

    CircuitBreaker cb(cfg);
    const int threads = 4;
    const int per_thread = 250;
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            while (!start.load()) { /* spin */ }
            for (int i = 0; i < per_thread; i++) {
                cb.recordSuccess();
            }
        });
    }

    start.store(true);
    for (auto& w : workers) { w.join(); }

    auto stats = cb.getStats();
    EXPECT_EQ(stats.successful_requests, threads * per_thread);
    EXPECT_EQ(stats.total_requests, threads * per_thread);
}

// ── Config Update Edge Cases ──────────────────────────────────────────────────

TEST(CircuitBreakerEdgeTest, UpdateConfig_DuringOperation) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 5;

    CircuitBreaker cb(cfg);
    cb.recordFailure();
    cb.recordFailure();

    // Update config to be more aggressive
    CircuitBreakerConfig new_cfg;
    new_cfg.failure_threshold = 2;
    new_cfg.failure_rate_threshold = 0.0;
    cb.updateConfig(new_cfg);

    // Config is updated but existing stats are preserved
    auto stats = cb.getStats();
    EXPECT_EQ(stats.failed_requests, 2);
    EXPECT_EQ(stats.total_requests, 2);
}

TEST(CircuitBreakerEdgeTest, RepeatedOpen_StatsResetOnClose) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 1;
    cfg.timeout = 10ms;
    cfg.success_threshold = 1;

    CircuitBreaker cb(cfg);

    // First cycle: CLOSED → OPEN → (wait) → HALF_OPEN → CLOSED
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(cb.isRequestAllowed());       // → HALF_OPEN
    cb.recordSuccess();                      // → CLOSED (stats RESET here!)

    // After transitionToClosed, stats are intentionally reset to start a new window
    auto stats_after_close = cb.getStats();
    EXPECT_EQ(stats_after_close.total_requests, 0);
    EXPECT_EQ(stats_after_close.failed_requests, 0);

    // Second cycle: CLOSED → OPEN → HALF_OPEN
    cb.recordFailure();
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);

    // Stats should reflect only the current cycle
    auto stats_cycle2 = cb.getStats();
    EXPECT_EQ(stats_cycle2.failed_requests, 1);
    EXPECT_EQ(stats_cycle2.total_requests, 1);
}

// ── Manager Edge Cases ────────────────────────────────────────────────────────

TEST(CircuitBreakerManagerEdgeTest, GetOrCreate_SameInstance) {
    auto& mgr = CircuitBreakerManager::getInstance();
    auto cb1 = mgr.getCircuitBreaker("test-service");
    auto cb2 = mgr.getCircuitBreaker("test-service");
    EXPECT_EQ(cb1, cb2);  // Same pointer for same service
}

TEST(CircuitBreakerManagerEdgeTest, Remove_NonExistent_NoCrash) {
    auto& mgr = CircuitBreakerManager::getInstance();
    EXPECT_NO_THROW(mgr.removeCircuitBreaker("non-existent-service"));
}

TEST(CircuitBreakerManagerEdgeTest, UpdateConfig_NonExistent_NoCrash) {
    auto& mgr = CircuitBreakerManager::getInstance();
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 10;
    EXPECT_NO_THROW(mgr.updateConfig("non-existent-service", cfg));
}

TEST(CircuitBreakerManagerEdgeTest, ResetAll_ClearsAllStates) {
    auto& mgr = CircuitBreakerManager::getInstance();
    auto cb1 = mgr.getCircuitBreaker("svc1");
    auto cb2 = mgr.getCircuitBreaker("svc2");

    cb1->recordFailure();
    cb1->recordFailure();
    cb2->recordFailure();

    mgr.resetAll();

    auto states = mgr.getAllStates();
    for (const auto& [svc, state] : states) {
        EXPECT_EQ(state, CircuitState::CLOSED);
    }
}

// ── Failure Rate Edge Cases ───────────────────────────────────────────────────

TEST(CircuitBreakerEdgeTest, FailureRate_AllSuccess_NoTransition) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 2;
    cfg.min_request_count = 3;

    CircuitBreaker cb(cfg);
    cb.recordSuccess();
    cb.recordSuccess();
    cb.recordSuccess();

    EXPECT_EQ(cb.getState(), CircuitState::CLOSED);
    auto stats = cb.getStats();
    EXPECT_EQ(stats.current_failure_rate, 0.0);
}

TEST(CircuitBreakerEdgeTest, FailureRate_AllFail_TransitionToOpen) {
    CircuitBreakerConfig cfg;
    cfg.failure_threshold = 2;
    cfg.failure_rate_threshold = 0.6;
    cfg.min_request_count = 3;

    CircuitBreaker cb(cfg);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();

    // 4/4 failures = 1.0 >= 0.6 threshold AND 4 >= min_request_count(3)
    EXPECT_EQ(cb.getState(), CircuitState::OPEN);
}
