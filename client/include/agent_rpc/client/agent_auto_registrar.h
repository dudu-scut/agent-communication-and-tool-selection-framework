/**
 * @file agent_auto_registrar.h
 * @brief Agent lifecycle manager — gRPC Register/Heartbeat/Unregister (P0-2)
 *
 * Encapsulates the full gRPC registration lifecycle for an Agent:
 *   start()  → RegisterAgent() + background heartbeat thread
 *   stop()   → UnregisterAgent() + thread join
 *
 * Usage:
 *   AgentAutoRegistrar registrar("localhost:50051", "math-agent", "0.0.0.0", 8080,
 *                                {"math", "algebra"}, {"fast"});
 *   registrar.start();
 *   // ... agent does work ...
 *   registrar.stop();
 */

#pragma once

#include "agent_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

namespace agent_rpc {
namespace client {

struct AgentRegistrationInfo {
    std::string name;                           // service_name (e.g. "math-agent")
    std::string version = "1.0.0";
    std::string host;                           // agent listen host
    int port = 0;                               // agent listen port
    std::vector<std::string> skills;            // AgentCard skill names
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> metadata;
    std::string agent_card_json;                // full AgentCard JSON (optional)
};

class AgentAutoRegistrar {
public:
    /**
     * @param server_address  gRPC server address (e.g. "localhost:50051")
     * @param info            Agent metadata
     * @param heartbeat_sec   Heartbeat interval in seconds (default 15)
     */
    AgentAutoRegistrar(const std::string& server_address,
                       const AgentRegistrationInfo& info,
                       int heartbeat_sec = 15);
    ~AgentAutoRegistrar();

    // Non-copyable
    AgentAutoRegistrar(const AgentAutoRegistrar&) = delete;
    AgentAutoRegistrar& operator=(const AgentAutoRegistrar&) = delete;

    /**
     * @brief Register with server and start heartbeat loop
     * @return true if RegisterAgent() succeeded
     */
    bool start();

    /**
     * @brief Unregister from server and stop heartbeat loop
     */
    void stop();

    /**
     * @brief Check if registration is active
     */
    bool isRegistered() const { return registered_; }

    /**
     * @brief Get the agent_id assigned by the server
     */
    const std::string& agentId() const { return agent_id_; }

private:
    void heartbeatLoop();
    bool sendRegister();
    bool sendHeartbeat();
    bool sendUnregister();

    std::string server_address_;
    AgentRegistrationInfo info_;
    int heartbeat_sec_;

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<agent_communication::AgentCommunicationService::Stub> stub_;
    std::string agent_id_;

    std::atomic<bool> registered_{false};
    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::mutex mutex_;
};

} // namespace client
} // namespace agent_rpc
