#pragma once

#include "types.h"
#include "logger.h"
#include "metrics.h"
#include "load_balancer.h"
#include "circuit_breaker.h"
#include <memory>
#include <string>

namespace agent_rpc {
namespace common {

class RpcFramework;

class RpcFramework {
public:
    static RpcFramework& getInstance();
    
    bool initialize(const RpcConfig& config);
    
    bool startServer();
    
    void stopServer();
    
    const RpcConfig& getConfig() const { return config_; }
    
    bool isRunning() const { return running_; }
    
    std::shared_ptr<Logger> getLogger();
    
    std::shared_ptr<Metrics> getMetrics();
    
    std::shared_ptr<LoadBalancer> getLoadBalancer();

private:
    RpcFramework() = default;
    ~RpcFramework() = default;
    RpcFramework(const RpcFramework&) = delete;
    RpcFramework& operator=(const RpcFramework&) = delete;
    
    RpcConfig config_;
    std::atomic<bool> running_{false};
    
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<Metrics> metrics_;
    std::shared_ptr<LoadBalancer> load_balancer_;
};

} // namespace common
} // namespace agent_rpc
