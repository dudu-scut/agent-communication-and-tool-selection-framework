/**
 * @file rpc_framework.cpp
 * @brief RpcFramework implementation stub (Fix #16)
 *
 * This module is not yet fully implemented. It provides the minimal
 * linkage surface so that the project compiles. Full implementation
 * is tracked by the RpcFramework class declaration in
 * common/include/agent_rpc/common/rpc_framework.h
 */

#include "agent_rpc/common/rpc_framework.h"

namespace agent_rpc {
namespace common {

RpcFramework& RpcFramework::getInstance() {
    static RpcFramework instance;
    return instance;
}

bool RpcFramework::initialize(const RpcConfig& config) {
    config_ = config;
    running_ = true;
    return true;
}

bool RpcFramework::startServer() {
    // TODO: Implement full server startup logic
    return running_;
}

void RpcFramework::stopServer() {
    running_ = false;
}

std::shared_ptr<Logger> RpcFramework::getLogger() {
    return logger_;
}

std::shared_ptr<Metrics> RpcFramework::getMetrics() {
    return metrics_;
}

std::shared_ptr<LoadBalancer> RpcFramework::getLoadBalancer() {
    return load_balancer_;
}

} // namespace common
} // namespace agent_rpc
