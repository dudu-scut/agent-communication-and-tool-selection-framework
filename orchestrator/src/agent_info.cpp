/**
 * @file agent_info.cpp
 * @brief AgentInfo implementation
 */

#include "agent_rpc/orchestrator/agent_info.h"

namespace agent_rpc {
namespace orchestrator {

// AgentInfo methods are all inline (hasSkill, hasTag, etc.)
// Agent population is now done via gRPC RegisterAgent → AgentRouter::addAgent()

} // namespace orchestrator
} // namespace agent_rpc
