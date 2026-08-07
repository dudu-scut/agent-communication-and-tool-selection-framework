#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "agent_rpc/common/postgres_store.h"

namespace {

TEST(PostgresPoolContractTest, CoordinatorRejectsPoolSizesOutsideOneToTen) {
    EXPECT_THROW((agent_rpc::common::detail::PostgresPoolLeaseCoordinator{0}), std::invalid_argument);
    EXPECT_THROW((agent_rpc::common::detail::PostgresPoolLeaseCoordinator{11}), std::invalid_argument);
}
TEST(PostgresPoolContractTest, ConcurrentLeasesDoNotReuseSlots) {
    constexpr int kPoolSize = 10;
    agent_rpc::common::detail::PostgresPoolLeaseCoordinator coordinator{kPoolSize};

    std::mutex state_mutex;
    std::condition_variable state_condition;
    std::vector<std::size_t> leases;
    int acquired = 0;
    bool release = false;

    std::vector<std::thread> workers;
    workers.reserve(kPoolSize);
    for (int worker = 0; worker < kPoolSize; ++worker) {
        workers.emplace_back([&] {
            const std::size_t lease = coordinator.acquire();
            {
                std::unique_lock lock(state_mutex);
                leases.push_back(lease);
                ++acquired;
                state_condition.notify_one();
                state_condition.wait(lock, [&] { return release; });
            }
            coordinator.release(lease);
        });
    }

    bool all_acquired = false;
    {
        std::unique_lock lock(state_mutex);
        all_acquired = state_condition.wait_for(
            lock, std::chrono::seconds(2), [&] { return acquired == kPoolSize; });
    }

    EXPECT_TRUE(all_acquired);
    EXPECT_EQ(leases.size(), static_cast<std::size_t>(kPoolSize));
    EXPECT_EQ(std::set<std::size_t>(leases.begin(), leases.end()).size(),
              static_cast<std::size_t>(kPoolSize));

    {
        std::lock_guard lock(state_mutex);
        release = true;
    }
    state_condition.notify_all();
    for (auto& worker : workers) {
        worker.join();
    }
}

}  // namespace
