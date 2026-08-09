#pragma once

#include "agent_rpc/common/types.h"
#include "agent_rpc/common/logger.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <array>
#include <chrono>

namespace agent_rpc {
namespace registry {

// Agent Live Metrics Types

/** Per-agent health metrics tracked in the registry module. */
struct AgentLiveMetrics {
    std::array<bool, 100> recent_results{};
    int buffer_idx = 0;
    int total_writes = 0;  // tracks total writes to determine if buffer has wrapped
    double ema_latency_ms = 0.0; // alpha=0.1
    std::atomic<int> active_requests{0};
    std::chrono::steady_clock::time_point last_heartbeat;
};

/** Health classification returned by evaluateHealth(). */
enum class HealthStatus { HEALTHY, DEGRADED, UNHEALTHY, UNKNOWN };

// Service registry interface
class ServiceRegistry {
public:
    virtual ~ServiceRegistry() = default;
    
    // Register a service
    virtual bool registerService(const common::ServiceEndpoint& endpoint) = 0;
    
    // Unregister a service
    virtual bool unregisterService(const std::string& service_id) = 0;
    
    // Discover services
    virtual std::vector<common::ServiceEndpoint> discoverServices(const std::string& service_name) = 0;
    
    // Get service health status
    virtual bool isServiceHealthy(const std::string& service_id) = 0;
    
    // Update service heartbeat
    virtual bool updateHeartbeat(const std::string& service_id) = 0;
    
    // Watch service changes
    virtual void watchServices(const std::string& service_name,
                              std::function<void(const std::vector<common::ServiceEndpoint>&)> callback) = 0;

    // Agent Live Metrics

    /**
     * @brief Record one agent call outcome for live metrics.
     * @param agent_id Agent identifier
     * @param success Whether the call succeeded
     * @param latency_ms Call duration in milliseconds
     */
    static void recordAgentCall(const std::string& agent_id, bool success, double latency_ms);

    /**
     * @brief Evaluate live health of a single agent.
     * @param agent_id Agent to evaluate
     * @return HealthStatus classification
     */
    static HealthStatus evaluateHealth(const std::string& agent_id);

    /**
     * @brief Iterate all tracked agents and evaluate health.
     * Calls evaluateHealth() per agent and logs alerts for degraded/unhealthy.
     * Intended to be called periodically by BackgroundScheduler.
     */
    static void evaluateAllHealth();

    /**
     * @brief Get a singleton reference (default MemoryServiceRegistry).
     */
    static ServiceRegistry& instance();
};

// Consul-based service registry implementation
class ConsulServiceRegistry : public ServiceRegistry {
public:
    ConsulServiceRegistry();
    ~ConsulServiceRegistry();
    
    // Initialization
    bool initialize(const std::string& consul_address);
    
    // Interface method implementations
    bool registerService(const common::ServiceEndpoint& endpoint) override;
    bool unregisterService(const std::string& service_id) override;
    std::vector<common::ServiceEndpoint> discoverServices(const std::string& service_name) override;
    bool isServiceHealthy(const std::string& service_id) override;
    bool updateHeartbeat(const std::string& service_id) override;
    void watchServices(const std::string& service_name,
                      std::function<void(const std::vector<common::ServiceEndpoint>&)> callback) override;
    
    // Health check
    void startHealthCheck();
    void stopHealthCheck();
    
    // Get the service ID
    std::string getServiceId(const common::ServiceEndpoint& endpoint) const;

private:
    void healthCheckLoop();
    std::string makeHttpRequest(const std::string& method, 
                               const std::string& url, 
                               const std::string& body = "");
    std::vector<common::ServiceEndpoint> parseServiceList(const std::string& json_response);
    common::ServiceEndpoint parseServiceEndpoint(const std::string& json_service);
    
    std::string consul_address_;
    std::atomic<bool> health_check_running_{false};
    std::thread health_check_thread_;
    
    mutable std::mutex services_mutex_;
    std::map<std::string, common::ServiceEndpoint> registered_services_;
    std::map<std::string, std::vector<common::ServiceEndpoint>> discovered_services_;
    
    std::map<std::string, std::function<void(const std::vector<common::ServiceEndpoint>&)>> watchers_;
    mutable std::mutex watchers_mutex_;
};

// Etcd-based service registry implementation
class EtcdServiceRegistry : public ServiceRegistry {
public:
    EtcdServiceRegistry();
    ~EtcdServiceRegistry();
    
    // Initialization
    bool initialize(const std::string& etcd_address);
    
    // Interface method implementations
    bool registerService(const common::ServiceEndpoint& endpoint) override;
    bool unregisterService(const std::string& service_id) override;
    std::vector<common::ServiceEndpoint> discoverServices(const std::string& service_name) override;
    bool isServiceHealthy(const std::string& service_id) override;
    bool updateHeartbeat(const std::string& service_id) override;
    void watchServices(const std::string& service_name,
                      std::function<void(const std::vector<common::ServiceEndpoint>&)> callback) override;

private:
    void watchLoop();
    std::string makeEtcdRequest(const std::string& method, 
                               const std::string& key, 
                               const std::string& value = "");
    std::vector<common::ServiceEndpoint> parseEtcdResponse(const std::string& response);
    
    std::string etcd_address_;
    std::atomic<bool> watch_running_{false};
    std::thread watch_thread_;
    
    mutable std::mutex services_mutex_;
    std::map<std::string, common::ServiceEndpoint> registered_services_;
    std::map<std::string, std::vector<common::ServiceEndpoint>> discovered_services_;
    
    std::map<std::string, std::function<void(const std::vector<common::ServiceEndpoint>&)>> watchers_;
    mutable std::mutex watchers_mutex_;
};

// In-memory service registry implementation (for testing)
class MemoryServiceRegistry : public ServiceRegistry {
public:
    MemoryServiceRegistry() = default;
    ~MemoryServiceRegistry() = default;
    
    // Interface method implementations
    bool registerService(const common::ServiceEndpoint& endpoint) override;
    bool unregisterService(const std::string& service_id) override;
    std::vector<common::ServiceEndpoint> discoverServices(const std::string& service_name) override;
    bool isServiceHealthy(const std::string& service_id) override;
    bool updateHeartbeat(const std::string& service_id) override;
    void watchServices(const std::string& service_name,
                      std::function<void(const std::vector<common::ServiceEndpoint>&)> callback) override;

private:
    mutable std::mutex services_mutex_;
    std::map<std::string, common::ServiceEndpoint> services_;
    std::map<std::string, std::function<void(const std::vector<common::ServiceEndpoint>&)>> watchers_;
    mutable std::mutex watchers_mutex_;
};

} // namespace registry
} // namespace agent_rpc
