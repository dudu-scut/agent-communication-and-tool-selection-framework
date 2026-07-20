/**
 * @file test_task_state_validator.cpp
 * @brief Edge-case tests for TaskStateValidator state machine
 *
 * Risk analysis: isValidTransition and getValidNextStates contain
 * duplicate switch logic that could diverge. Tests verify consistency
 * between the two functions and cover all state combinations.
 */

#include <gtest/gtest.h>
#include "agent_rpc/a2a_adapter/task_manager_wrapper.h"

using namespace agent_rpc::a2a_adapter;

// ── Valid Transitions ─────────────────────────────────────────────────────────

TEST(TaskStateValidatorTest, Submitted_ValidTransitions) {
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Submitted, a2a::TaskState::Running));
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Submitted, a2a::TaskState::Canceled));
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Submitted, a2a::TaskState::Rejected));
}

TEST(TaskStateValidatorTest, Running_ValidTransitions) {
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Running, a2a::TaskState::Completed));
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Running, a2a::TaskState::Failed));
    EXPECT_TRUE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Running, a2a::TaskState::Canceled));
}

// ── Terminal State Transitions (all should be invalid) ────────────────────────

TEST(TaskStateValidatorTest, TerminalStates_CannotTransition) {
    std::vector<a2a::TaskState> terminals = {
        a2a::TaskState::Completed,
        a2a::TaskState::Failed,
        a2a::TaskState::Canceled,
        a2a::TaskState::Rejected
    };

    std::vector<a2a::TaskState> all_states = {
        a2a::TaskState::Submitted,
        a2a::TaskState::Running,
        a2a::TaskState::Completed,
        a2a::TaskState::Failed,
        a2a::TaskState::Canceled,
        a2a::TaskState::Rejected
    };

    for (auto from : terminals) {
        for (auto to : all_states) {
            EXPECT_FALSE(TaskStateValidator::isValidTransition(from, to))
                << "Terminal state should not transition to any other state";
        }
    }
}

// ── Invalid Transitions ───────────────────────────────────────────────────────

TEST(TaskStateValidatorTest, Submitted_InvalidTransitions) {
    // Submitted cannot go directly to Completed or Failed
    EXPECT_FALSE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Submitted, a2a::TaskState::Completed));
    EXPECT_FALSE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Submitted, a2a::TaskState::Failed));
}

TEST(TaskStateValidatorTest, Running_InvalidTransitions) {
    // Running cannot go back to Submitted or to Rejected
    EXPECT_FALSE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Running, a2a::TaskState::Submitted));
    EXPECT_FALSE(TaskStateValidator::isValidTransition(
        a2a::TaskState::Running, a2a::TaskState::Rejected));
}

// ── Consistency: isValidTransition ↔ getValidNextStates ──────────────────────

TEST(TaskStateValidatorTest, Consistency_AllTransitionsMatch) {
    std::vector<a2a::TaskState> all_states = {
        a2a::TaskState::Submitted,
        a2a::TaskState::Running,
        a2a::TaskState::Completed,
        a2a::TaskState::Failed,
        a2a::TaskState::Canceled,
        a2a::TaskState::Rejected
    };

    for (auto from : all_states) {
        auto valid_next = TaskStateValidator::getValidNextStates(from);

        for (auto to : all_states) {
            bool via_isValid = TaskStateValidator::isValidTransition(from, to);
            bool via_getValid = std::find(valid_next.begin(), valid_next.end(), to) != valid_next.end();

            EXPECT_EQ(via_isValid, via_getValid)
                << "Inconsistency: isValidTransition(" << static_cast<int>(from)
                << "→" << static_cast<int>(to) << ") = " << via_isValid
                << " but getValidNextStates(" << static_cast<int>(from)
                << ") " << (via_getValid ? "contains" : "does not contain")
                << " " << static_cast<int>(to);
        }
    }
}

// ── Terminal States ───────────────────────────────────────────────────────────

TEST(TaskStateValidatorTest, IsTerminalState) {
    EXPECT_TRUE(TaskStateValidator::isTerminalState(a2a::TaskState::Completed));
    EXPECT_TRUE(TaskStateValidator::isTerminalState(a2a::TaskState::Failed));
    EXPECT_TRUE(TaskStateValidator::isTerminalState(a2a::TaskState::Canceled));
    EXPECT_TRUE(TaskStateValidator::isTerminalState(a2a::TaskState::Rejected));

    EXPECT_FALSE(TaskStateValidator::isTerminalState(a2a::TaskState::Submitted));
    EXPECT_FALSE(TaskStateValidator::isTerminalState(a2a::TaskState::Running));
}

// ── Edge: Self-transitions ────────────────────────────────────────────────────

TEST(TaskStateValidatorTest, SelfTransitions_AllInvalid) {
    std::vector<a2a::TaskState> all_states = {
        a2a::TaskState::Submitted,
        a2a::TaskState::Running,
        a2a::TaskState::Completed,
        a2a::TaskState::Failed,
        a2a::TaskState::Canceled,
        a2a::TaskState::Rejected
    };

    for (auto state : all_states) {
        EXPECT_FALSE(TaskStateValidator::isValidTransition(state, state))
            << "Self-transition should be invalid for state " << static_cast<int>(state);
    }
}

// ── Edge: getValidNextStates from terminal states returns empty ────────────────

TEST(TaskStateValidatorTest, GetValidNextStates_TerminalReturnsEmpty) {
    EXPECT_TRUE(TaskStateValidator::getValidNextStates(a2a::TaskState::Completed).empty());
    EXPECT_TRUE(TaskStateValidator::getValidNextStates(a2a::TaskState::Failed).empty());
    EXPECT_TRUE(TaskStateValidator::getValidNextStates(a2a::TaskState::Canceled).empty());
    EXPECT_TRUE(TaskStateValidator::getValidNextStates(a2a::TaskState::Rejected).empty());
}
