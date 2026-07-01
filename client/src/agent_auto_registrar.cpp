/**
 * @file agent_auto_registrar.cpp
 * @brief AgentAutoRegistrar implementation — gRPC registration lifecycle (P0-2)
 */

#include "agent_rpc/client/agent_auto_registrar.h"
#include "agent_rpc/common/logger.h"
#include "common.pb.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <chrono>

namespace agent_rpc {
namespace client {

AgentAutoRegistrar::AgentAutoRegistrar(
    const std::string& server_address,
    const AgentRegistrationInfo& info,
    int heartbeat_sec)
    : server_address_(server_address)
    , info_(info)
    , heartbeat_sec_(heartbeat_sec) {
}

AgentAutoRegistrar::~AgentAutoRegistrar() {
    stop();
}

bool AgentAutoRegistrar::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registered_) return true;

    // Create gRPC channel and stub
    channel_ = grpc::CreateChannel(server_address_, grpc::InsecureChannelCredentials());
    stub_ = agent_communication::AgentCommunicationService::NewStub(channel_);

    if (!sendRegister()) {
        LOG_ERROR("AgentAutoRegistrar: RegisterAgent failed for " + info_.name);
        stub_.reset();
        channel_.reset();
        return false;
    }

    registered_ = true;
    running_ = true;
    heartbeat_thread_ = std::thread(&AgentAutoRegistrar::heartbeatLoop, this);

    LOG_INFO("AgentAutoRegistrar: registered as " + agent_id_ +
             " (heartbeat every " + std::to_string(heartbeat_sec_) + "s)");
    return true;
}

void AgentAutoRegistrar::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!registered_) return;
        running_ = false;
    }

    // Wait for heartbeat thread to finish
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // Send unregister
    if (stub_) {
        sendUnregister();
    }

    registered_ = false;
    LOG_INFO("AgentAutoRegistrar: unregistered " + agent_id_);

    stub_.reset();
    channel_.reset();
}

// ============================================================================
// Private
// ============================================================================

bool AgentAutoRegistrar::sendRegister() {
    agent_communication::RegisterAgentRequest request;
    auto* si = request.mutable_agent_info();
    si->set_service_name(info_.name);
    si->set_version(info_.version);
    si->set_host(info_.host);
    si->set_port(info_.port);
    for (const auto& s : info_.skills) si->add_skills(s);
    for (const auto& t : info_.tags) si->add_tags(t);
    for (const auto& [k, v] : info_.metadata) (*si->mutable_metadata())[k] = v;
    if (!info_.agent_card_json.empty()) si->set_agent_card(info_.agent_card_json);
    request.set_heartbeat_interval(heartbeat_sec_);

    agent_communication::RegisterAgentResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));

    auto status = stub_->RegisterAgent(&context, request, &response);
    if (!status.ok()) {
        LOG_ERROR("RegisterAgent RPC failed: " + status.error_message());
        return false;
    }
    if (response.status().code() != 0) {
        LOG_ERROR("RegisterAgent returned error: " + response.status().message());
        return false;
    }

    agent_id_ = response.agent_id();
    return true;
}

bool AgentAutoRegistrar::sendHeartbeat() {
    agent_communication::HeartbeatRequest request;
    request.set_agent_id(agent_id_);

    agent_communication::HeartbeatResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = stub_->Heartbeat(&context, request, &response);
    if (!status.ok()) {
        LOG_WARN("Heartbeat failed for " + agent_id_ + ": " + status.error_message());
        return false;
    }
    return true;
}

bool AgentAutoRegistrar::sendUnregister() {
    agent_communication::UnregisterAgentRequest request;
    request.set_agent_id(agent_id_);
    request.set_reason("shutdown");

    agent_communication::UnregisterAgentResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = stub_->UnregisterAgent(&context, request, &response);
    if (!status.ok()) {
        LOG_WARN("UnregisterAgent failed: " + status.error_message());
        return false;
    }
    return true;
}

void AgentAutoRegistrar::heartbeatLoop() {
    while (running_) {
        // Sleep in small increments so we can respond to stop() quickly
        for (int i = 0; i < heartbeat_sec_ * 10 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (running_) {
            sendHeartbeat();
        }
    }
}

} // namespace client
} // namespace agent_rpc
