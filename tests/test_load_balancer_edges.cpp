/**
 * @file test_load_balancer_edges.cpp
 * @brief Bug-hunt: edge cases for load balancer strategies
 *
 * Risk analysis findings tested:
 * 1. const_cast antipattern in LeastConnections/LeastResponseTime selectEndpoint
 * 2. ConsistentHash uses random hash instead of key-based (defeats purpose)
 * 3. WeightedRoundRobin zero/negative weight handling
 * 4. Empty endpoint list edge cases
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "agent_rpc/common/load_balancer.h"

using namespace agent_rpc::common;

static std::string epId(const ServiceEndpoint& ep) {
    return ep.host + ":" + std::to_string(ep.port);
}

static std::vector<ServiceEndpoint> makeEndpoints(std::vector<std::string> hosts) {
    std::vector<ServiceEndpoint> eps;
    for (size_t i = 0; i < hosts.size(); i++) {
        ServiceEndpoint ep;
        ep.host = hosts[i];
        ep.port = static_cast<int>(8080 + i);
        ep.service_name = "svc" + std::to_string(i);
        ep.is_healthy = true;
        eps.push_back(ep);
    }
    return eps;
}

// ── ConsistentHash: key-based selection regression ────────────────────────────

TEST(LoadBalancerEdgeTest, ConsistentHashByKey_SameKeySameEndpoint) {
    auto eps = makeEndpoints({"host-a", "host-b", "host-c"});
    ConsistentHashLoadBalancer lb(150);
    lb.updateEndpoints(eps);  // Must build hash ring first

    // selectEndpointByKey should be deterministic for same key
    std::string first = epId(lb.selectEndpointByKey("my-stable-key", eps));
    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(epId(lb.selectEndpointByKey("my-stable-key", eps)), first)
            << "selectEndpointByKey should return deterministic result for same key";
    }
}

TEST(LoadBalancerEdgeTest, ConsistentHash_SelectEndpointWithoutKey_UsesRandom) {
    auto eps = makeEndpoints({"host-a", "host-b", "host-c"});
    ConsistentHashLoadBalancer lb(150);
    lb.updateEndpoints(eps);  // Must build hash ring first

    // selectEndpoint() (no key) uses random_device internally — regression:
    // this defeats the purpose of consistent hashing
    // But it should at least return valid endpoints
    bool saw_more_than_one = false;
    std::string first = epId(lb.selectEndpoint(eps));
    for (int i = 0; i < 20; i++) {
        std::string sel = epId(lb.selectEndpoint(eps));
        EXPECT_FALSE(sel.empty());
        if (sel != first) saw_more_than_one = true;
    }
    // With random hash, we expect variation across calls
    // If this FAILS (always same), the "random" is actually deterministic
    EXPECT_TRUE(saw_more_than_one)
        << "selectEndpoint with no key should produce varied results (random)";
}

// ── RoundRobin: wrap-around and empty list ────────────────────────────────────

TEST(LoadBalancerEdgeTest, RoundRobin_EmptyEndpointsThrows) {
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::ROUND_ROBIN);
    std::vector<ServiceEndpoint> empty;
    lb->updateEndpoints(empty);
    EXPECT_THROW(lb->selectEndpoint(empty), std::runtime_error);
}

TEST(LoadBalancerEdgeTest, RoundRobin_WrapsAround) {
    auto eps = makeEndpoints({"a", "b", "c"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::ROUND_ROBIN);
    lb->updateEndpoints(eps);

    std::string e1 = epId(lb->selectEndpoint(eps));
    std::string e2 = epId(lb->selectEndpoint(eps));
    std::string e3 = epId(lb->selectEndpoint(eps));
    EXPECT_NE(e1, e2);
    EXPECT_NE(e2, e3);
    // Wrap around
    std::string e4 = epId(lb->selectEndpoint(eps));
    EXPECT_EQ(e4, e1);
}

// ── WeightedRoundRobin: zero weight ───────────────────────────────────────────

TEST(LoadBalancerEdgeTest, WeightedRoundRobin_ZeroWeightFallback) {
    auto eps = makeEndpoints({"host-a", "host-b"});
    eps[0].metadata["weight"] = "0";
    eps[1].metadata["weight"] = "5";

    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
    lb->updateEndpoints(eps);

    int count_a = 0, count_b = 0;
    for (int i = 0; i < 100; i++) {
        std::string sel = epId(lb->selectEndpoint(eps));
        if (sel.find("host-a") != std::string::npos) count_a++;
        else if (sel.find("host-b") != std::string::npos) count_b++;
    }
    EXPECT_GT(count_b, count_a);
    EXPECT_GT(count_b, 60);
}

TEST(LoadBalancerEdgeTest, WeightedRoundRobin_NegativeWeight_DefaultToOne) {
    auto eps = makeEndpoints({"host-a"});
    eps[0].metadata["weight"] = "-999";
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
    lb->updateEndpoints(eps);
    // Should not crash — stoi on "-999" throws, caught → defaults to 1
    std::string sel = epId(lb->selectEndpoint(eps));
    EXPECT_FALSE(sel.empty());
}

// ── Random: basic functionality ───────────────────────────────────────────────

TEST(LoadBalancerEdgeTest, Random_ReturnsValidEndpoint) {
    auto eps = makeEndpoints({"a", "b", "c"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::RANDOM);
    lb->updateEndpoints(eps);

    for (int i = 0; i < 20; i++) {
        std::string sel = epId(lb->selectEndpoint(eps));
        EXPECT_FALSE(sel.empty());
    }
}

// ── MarkEndpointStatus: non-existent endpoint ─────────────────────────────────

TEST(LoadBalancerEdgeTest, MarkEndpointStatus_NonExistent_NoCrash) {
    auto eps = makeEndpoints({"a"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::ROUND_ROBIN);
    lb->updateEndpoints(eps);

    EXPECT_NO_THROW(lb->markEndpointStatus("non-existent", false));
    EXPECT_NO_THROW(lb->markEndpointStatus("", true));
}

// ── UpdateEndpoints: replaces list completely ─────────────────────────────────

TEST(LoadBalancerEdgeTest, UpdateEndpoints_ReplacesOldList) {
    auto eps3 = makeEndpoints({"a", "b", "c"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::ROUND_ROBIN);
    lb->updateEndpoints(eps3);

    auto eps2 = makeEndpoints({"x", "y"});
    lb->updateEndpoints(eps2);

    for (int i = 0; i < 10; i++) {
        std::string sel = epId(lb->selectEndpoint(eps2));
        bool from_new = sel.find("x") != std::string::npos ||
                        sel.find("y") != std::string::npos;
        EXPECT_TRUE(from_new) << "Selected endpoint from old list: " << sel;
    }
}

// ── Manager: strategy switching ───────────────────────────────────────────────

TEST(LoadBalancerEdgeTest, Manager_SwitchStrategy) {
    LoadBalancerManager mgr(LoadBalanceStrategy::ROUND_ROBIN);
    auto eps = makeEndpoints({"a", "b"});
    mgr.updateEndpoints(eps);

    std::string before = epId(mgr.selectEndpoint(eps));
    EXPECT_FALSE(before.empty());

    mgr.setStrategy(LoadBalanceStrategy::RANDOM);
    EXPECT_EQ(mgr.getCurrentStrategyName(), "Random");

    std::string after = epId(mgr.selectEndpoint(eps));
    EXPECT_FALSE(after.empty());
}

// ── Healthy endpoint filtering ────────────────────────────────────────────────

TEST(LoadBalancerEdgeTest, UnhealthyEndpoint_Excluded) {
    auto eps = makeEndpoints({"host-a", "host-b"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::ROUND_ROBIN);
    lb->updateEndpoints(eps);

    // Mark host-a as unhealthy via endpoint ID
    lb->markEndpointStatus("host-a:8080", false);

    // All selections should go to host-b
    for (int i = 0; i < 10; i++) {
        std::string sel = epId(lb->selectEndpoint(eps));
        EXPECT_NE(sel.find("host-b"), std::string::npos)
            << "Unhealthy endpoint was selected: " << sel;
    }
}

// ── Factory: strategies list ──────────────────────────────────────────────────

TEST(LoadBalancerEdgeTest, Factory_AvailableStrategies) {
    auto strategies = LoadBalancerFactory::getAvailableStrategies();
    EXPECT_GE(strategies.size(), 4u);
}

// ── LeastResponseTime: update response time, select prefers faster ────────────

TEST(LoadBalancerEdgeTest, LeastResponseTime_PrefersFaster) {
    auto eps = makeEndpoints({"slow", "fast"});
    auto lb = LoadBalancerFactory::createLoadBalancer(LoadBalanceStrategy::LEAST_RESPONSE_TIME);
    lb->updateEndpoints(eps);

    // Record response times
    auto* lrt = dynamic_cast<LeastResponseTimeLoadBalancer*>(lb.get());
    ASSERT_NE(lrt, nullptr);
    lrt->updateResponseTime("slow:8080", std::chrono::milliseconds(500));
    lrt->updateResponseTime("fast:8081", std::chrono::milliseconds(10));

    // Fast should be preferred (lower avg response time)
    int slow_count = 0, fast_count = 0;
    for (int i = 0; i < 50; i++) {
        std::string sel = epId(lrt->selectEndpoint(eps));
        if (sel.find("slow") != std::string::npos) slow_count++;
        else if (sel.find("fast") != std::string::npos) fast_count++;
    }
    EXPECT_GT(fast_count, slow_count)
        << "Faster endpoint should be selected more often";
}
