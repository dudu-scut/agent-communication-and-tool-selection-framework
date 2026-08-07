#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#ifndef NEXUSAI_AUTH_INTERCEPTOR_HEADER
#error "NEXUSAI_AUTH_INTERCEPTOR_HEADER must point at auth_interceptor.h"
#endif

#ifndef NEXUSAI_AUTH_INTERCEPTOR_SOURCE
#error "NEXUSAI_AUTH_INTERCEPTOR_SOURCE must point at auth_interceptor.cpp"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream source{path, std::ios::binary};
    EXPECT_TRUE(source.is_open()) << "cannot read " << path;
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

void expectContains(const std::string& source, const std::string& fragment) {
    EXPECT_NE(source.find(fragment), std::string::npos) << "missing: " << fragment;
}

void expectNotContains(const std::string& source, const std::string& fragment) {
    EXPECT_EQ(source.find(fragment), std::string::npos) << "forbidden: " << fragment;
}

TEST(AuthOwnerContextContractTest, ExposesOnlyTheOwnerAndTraceAccessors) {
    const std::string header = readFile(NEXUSAI_AUTH_INTERCEPTOR_HEADER);
    ASSERT_FALSE(header.empty());

    expectContains(header, "static std::string currentUserId();");
    expectContains(header, "static std::string currentUsername();");
    expectContains(header, "static std::string currentTraceId();");
    expectContains(header, "thread_local AuthContext tls_auth_");
    expectNotContains(header, "std::string token");
}

TEST(AuthOwnerContextContractTest, WhitelistsOnlyAuthBootstrapAndGrpcHealthMethods) {
    const std::string source = readFile(NEXUSAI_AUTH_INTERCEPTOR_SOURCE);
    ASSERT_FALSE(source.empty());

    for (const std::string method : {
             "/agent_communication.auth.UserService/Register",
             "/agent_communication.auth.UserService/Login",
             "/agent_communication.auth.UserService/ValidateToken",
             "/grpc.health.v1.Health/Check",
             "/grpc.health.v1.Health/Watch",
         }) {
        expectContains(source, method);
    }

    for (const std::string method : {
             "/agent_communication.HealthService/Check",
             "/agent_communication.HealthService/Watch",
             "/agent_communication.AgentCommunicationService/RegisterAgent",
             "/agent_communication.AgentCommunicationService/UnregisterAgent",
             "/agent_communication.AgentCommunicationService/Heartbeat",
             "/agent_communication.AgentCommunicationService/GetAgents",
         }) {
        expectNotContains(source, method);
    }
}

TEST(AuthOwnerContextContractTest, ValidatesAuthorizationAndTraceMetadataBeforeCreatingContext) {
    const std::string source = readFile(NEXUSAI_AUTH_INTERCEPTOR_SOURCE);
    ASSERT_FALSE(source.empty());

    expectContains(source, "Bearer ");
    expectContains(source, "64");
    expectContains(source, "256");
    expectContains(source, "0x20");
    expectContains(source, "0x7e");
    expectContains(source, "128");
    expectContains(source, "isValidBearerAuthorization");
    expectContains(source, "isAsciiHex");
    expectContains(source, "token_length < kMinBearerTokenLength");
    expectContains(source, "token_length > kMaxBearerTokenLength");
    expectContains(source, "value.compare(0, kBearerPrefixLength, kBearerPrefix)");
    expectContains(source, "isValidTraceId");
    expectContains(source, "value.size() > kMaxTraceIdLength");
    expectContains(source, "character < 0x20 || character > 0x7e");
    expectNotContains(source, "return value;");
    expectNotContains(source, "LOG_");
    expectNotContains(source, "tls_auth_.token");
}

TEST(AuthOwnerContextContractTest, DoesNotAuthenticateAnAnonymousProtectedCallWhenEnabled) {
    const std::string source = readFile(NEXUSAI_AUTH_INTERCEPTOR_SOURCE);
    ASSERT_FALSE(source.empty());

    expectContains(source, "if (!auth_enabled_.load(std::memory_order_relaxed)) return true;");
    expectContains(source, "return tls_auth_.authenticated;");
    expectContains(source, "tls_auth_ = AuthContext{};");
}

}  // namespace
