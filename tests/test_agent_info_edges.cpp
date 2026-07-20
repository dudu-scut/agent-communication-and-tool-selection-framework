/**
 * @file test_agent_info_edges.cpp
 * @brief Bug-hunt: edge cases for AgentInfo inline methods
 *
 * Risk analysis findings tested:
 * 1. hasAnySkill/hasAllSkills empty input returns true (semantic: vacuously true)
 * 2. getSkillDescription can't distinguish "not found" from "empty description"
 * 3. hasSkill/hasTag case sensitivity
 * 4. to_string/routing_strategy_from_string round-trip
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "agent_rpc/orchestrator/agent_info.h"

using namespace agent_rpc::orchestrator;

static AgentInfo makeAgent(const std::string& id = "test-1") {
    AgentInfo a;
    a.id = id;
    a.name = "TestAgent";
    return a;
}

// ── hasSkill ──────────────────────────────────────────────────────────────────

TEST(AgentInfoEdgeTest, HasSkill_ExactMatch) {
    auto a = makeAgent();
    a.skills = {"code-gen", "code-review", "deploy"};
    EXPECT_TRUE(a.hasSkill("code-gen"));
    EXPECT_TRUE(a.hasSkill("code-review"));
    EXPECT_TRUE(a.hasSkill("deploy"));
}

TEST(AgentInfoEdgeTest, HasSkill_NoMatch) {
    auto a = makeAgent();
    a.skills = {"code-gen"};
    EXPECT_FALSE(a.hasSkill("nonexistent"));
}

TEST(AgentInfoEdgeTest, HasSkill_EmptySkills) {
    auto a = makeAgent();
    EXPECT_FALSE(a.hasSkill("anything"));
}

TEST(AgentInfoEdgeTest, HasSkill_EmptyInput) {
    auto a = makeAgent();
    a.skills = {"code-gen", ""};  // Agent has empty string as a skill
    EXPECT_TRUE(a.hasSkill(""));
}

TEST(AgentInfoEdgeTest, HasSkill_CaseSensitive) {
    auto a = makeAgent();
    a.skills = {"Code-Gen"};
    EXPECT_FALSE(a.hasSkill("code-gen")) << "Skill matching should be case-sensitive";
    EXPECT_TRUE(a.hasSkill("Code-Gen"));
}

// ── hasTag ────────────────────────────────────────────────────────────────────

TEST(AgentInfoEdgeTest, HasTag_EmptyInput) {
    auto a = makeAgent();
    a.tags = {"tag1", ""};
    EXPECT_TRUE(a.hasTag(""));
    EXPECT_TRUE(a.hasTag("tag1"));
}

// ── hasAllSkills: empty requires = vacuously true ─────────────────────────────

TEST(AgentInfoEdgeTest, HasAllSkills_EmptyRequirements_ReturnsTrue) {
    auto a = makeAgent();
    a.skills = {};  // Agent has no skills
    EXPECT_TRUE(a.hasAllSkills({}));
}

TEST(AgentInfoEdgeTest, HasAllSkills_AllPresent) {
    auto a = makeAgent();
    a.skills = {"a", "b", "c"};
    EXPECT_TRUE(a.hasAllSkills({"a", "c"}));
}

TEST(AgentInfoEdgeTest, HasAllSkills_PartialMatch_ReturnsFalse) {
    auto a = makeAgent();
    a.skills = {"a", "b"};
    EXPECT_FALSE(a.hasAllSkills({"a", "c"}));
}

// ── hasAnySkill: empty requires = vacuously true ──────────────────────────────

TEST(AgentInfoEdgeTest, HasAnySkill_EmptyRequirements_ReturnsTrue) {
    auto a = makeAgent();
    EXPECT_TRUE(a.hasAnySkill({}));
}

TEST(AgentInfoEdgeTest, HasAnySkill_OneMatch) {
    auto a = makeAgent();
    a.skills = {"x", "y"};
    EXPECT_TRUE(a.hasAnySkill({"y"}));
}

TEST(AgentInfoEdgeTest, HasAnySkill_NoMatch) {
    auto a = makeAgent();
    a.skills = {"x"};
    EXPECT_FALSE(a.hasAnySkill({"y", "z"}));
}

// ── getSkillDescription: can't distinguish "not found" from "empty" ───────────

TEST(AgentInfoEdgeTest, GetSkillDescription_EmptyVsNotFound_Indistinguishable) {
    auto a = makeAgent();

    std::string not_found = a.getSkillDescription("nonexistent");
    a.skill_descriptions["exists-empty"] = "";
    std::string exists_empty = a.getSkillDescription("exists-empty");

    EXPECT_EQ(not_found, "");
    EXPECT_EQ(exists_empty, "");
    EXPECT_EQ(not_found, exists_empty)
        << "BUG: cannot distinguish 'not found' from 'empty description'";
}

TEST(AgentInfoEdgeTest, GetSkillDescription_ReturnsDescription) {
    auto a = makeAgent();
    a.skill_descriptions["code-gen"] = "Generates code from requirements";
    EXPECT_EQ(a.getSkillDescription("code-gen"), "Generates code from requirements");
}

// ── RoutingStrategy: to_string + from_string ──────────────────────────────────

TEST(AgentInfoEdgeTest, RoutingStrategy_ToString) {
    EXPECT_EQ(to_string(RoutingStrategy::ROUND_ROBIN), "round_robin");
    EXPECT_EQ(to_string(RoutingStrategy::RANDOM), "random");
    EXPECT_EQ(to_string(RoutingStrategy::SKILL_MATCH), "skill_match");
    EXPECT_EQ(to_string(RoutingStrategy::LEAST_LOAD), "least_load");
}

TEST(AgentInfoEdgeTest, RoutingStrategy_FromString) {
    EXPECT_EQ(routing_strategy_from_string("round_robin"), RoutingStrategy::ROUND_ROBIN);
    EXPECT_EQ(routing_strategy_from_string("random"), RoutingStrategy::RANDOM);
    EXPECT_EQ(routing_strategy_from_string("skill_match"), RoutingStrategy::SKILL_MATCH);
    EXPECT_EQ(routing_strategy_from_string("least_load"), RoutingStrategy::LEAST_LOAD);
}

TEST(AgentInfoEdgeTest, RoutingStrategy_UnknownString_Defaults) {
    EXPECT_EQ(routing_strategy_from_string("gibberish"), RoutingStrategy::SKILL_MATCH);
    EXPECT_EQ(routing_strategy_from_string(""), RoutingStrategy::SKILL_MATCH);
}

TEST(AgentInfoEdgeTest, RoutingStrategy_CaseSensitive) {
    // Case-sensitive — "SKILL_MATCH" doesn't match "skill_match"
    EXPECT_EQ(routing_strategy_from_string("SKILL_MATCH"), RoutingStrategy::SKILL_MATCH)
        << "Case mismatch defaults to SKILL_MATCH (which happens to be the same value)";
}
