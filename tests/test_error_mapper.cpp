/**
 * @file test_error_mapper.cpp
 * @brief Bug-hunt: edge cases for ErrorMapper A2A ↔ gRPC error mapping
 */

#include <gtest/gtest.h>
#include <string>
#include <stdexcept>
#include "agent_rpc/a2a_adapter/error_mapper.h"

using namespace agent_rpc::a2a_adapter;

// ── Known A2A error codes → gRPC ─────────────────────────────────────────────

TEST(ErrorMapperTest, MapKnownErrors_ToGrpc) {
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::InvalidRequest),
              grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::TaskNotFound),
              grpc::StatusCode::NOT_FOUND);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::TaskNotCancelable),
              grpc::StatusCode::FAILED_PRECONDITION);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::InternalError),
              grpc::StatusCode::INTERNAL);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::MethodNotFound),
              grpc::StatusCode::UNIMPLEMENTED);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::InvalidParams),
              grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(a2a::ErrorCode::ParseError),
              grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(ErrorMapperTest, MapToGrpcStatus_DefaultUnknown) {
    auto unknown = static_cast<a2a::ErrorCode>(999);
    EXPECT_EQ(ErrorMapper::mapToGrpcStatus(unknown), grpc::StatusCode::UNKNOWN);
}

// ── Network exception mapping ─────────────────────────────────────────────────

TEST(ErrorMapperTest, MapNetworkException_ConnectionRefused) {
    try {
        throw std::runtime_error("Connection refused");
    } catch (const std::exception& e) {
        EXPECT_EQ(ErrorMapper::mapNetworkException(e), grpc::StatusCode::UNAVAILABLE);
    }
}

TEST(ErrorMapperTest, MapNetworkException_Timeout) {
    try {
        throw std::runtime_error("Connection timed out");
    } catch (const std::exception& e) {
        EXPECT_EQ(ErrorMapper::mapNetworkException(e), grpc::StatusCode::DEADLINE_EXCEEDED);
    }
}

TEST(ErrorMapperTest, MapNetworkException_BrokenPipe) {
    try {
        throw std::runtime_error("Broken pipe");
    } catch (const std::exception& e) {
        EXPECT_EQ(ErrorMapper::mapNetworkException(e), grpc::StatusCode::UNAVAILABLE);
    }
}

TEST(ErrorMapperTest, MapNetworkException_EmptyMessage) {
    try {
        throw std::runtime_error("");
    } catch (const std::exception& e) {
        // Empty message falls through to default → INTERNAL
        EXPECT_EQ(ErrorMapper::mapNetworkException(e), grpc::StatusCode::INTERNAL);
    }
}

// ── Integer error code mapping (JSON-RPC) ─────────────────────────────────────

TEST(ErrorMapperTest, MapIntToGrpcStatus_KnownCodes) {
    EXPECT_EQ(ErrorMapper::mapIntToGrpcStatus(-32700), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(ErrorMapper::mapIntToGrpcStatus(-32601), grpc::StatusCode::UNIMPLEMENTED);
    EXPECT_EQ(ErrorMapper::mapIntToGrpcStatus(-32603), grpc::StatusCode::INTERNAL);
}

TEST(ErrorMapperTest, MapIntToGrpcStatus_UnknownCode) {
    EXPECT_EQ(ErrorMapper::mapIntToGrpcStatus(42), grpc::StatusCode::UNKNOWN);
    EXPECT_EQ(ErrorMapper::mapIntToGrpcStatus(0), grpc::StatusCode::UNKNOWN);
}

// ── Status creation ───────────────────────────────────────────────────────────

TEST(ErrorMapperTest, CreateGrpcStatus_WithMessage) {
    auto st = ErrorMapper::createGrpcStatus(
        a2a::ErrorCode::InternalError, "Something went wrong");
    EXPECT_EQ(st.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_EQ(st.error_message(), "Something went wrong");
}

TEST(ErrorMapperTest, CreateGrpcStatus_EmptyMessage_FallsBackToDesc) {
    auto st = ErrorMapper::createGrpcStatus(a2a::ErrorCode::TaskNotFound, "");
    EXPECT_EQ(st.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_FALSE(st.error_message().empty());
}

// ── Error description ─────────────────────────────────────────────────────────

TEST(ErrorMapperTest, GetErrorDescription_AllCodes) {
    std::vector<a2a::ErrorCode> codes = {
        a2a::ErrorCode::ParseError,
        a2a::ErrorCode::InvalidRequest,
        a2a::ErrorCode::MethodNotFound,
        a2a::ErrorCode::InvalidParams,
        a2a::ErrorCode::InternalError,
        a2a::ErrorCode::TaskNotFound,
        a2a::ErrorCode::TaskNotCancelable,
        a2a::ErrorCode::UnsupportedOperation,
    };
    for (auto code : codes) {
        EXPECT_FALSE(ErrorMapper::getErrorDescription(code).empty())
            << "Missing description for error code " << static_cast<int>(code);
    }
}
