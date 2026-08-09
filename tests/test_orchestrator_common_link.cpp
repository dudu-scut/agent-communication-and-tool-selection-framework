// Focused link regression for orchestrator's transitive common dependency.

#include "agent_rpc/orchestrator/agent_router.h"

int main() {
    agent_rpc::orchestrator::AgentRouter router;
    return router.initialize(agent_rpc::orchestrator::RoutingStrategy::ROUND_ROBIN)
               ? 0
               : 1;
}
