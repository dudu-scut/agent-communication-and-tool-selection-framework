#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#ifndef RPC_SERVER_SOURCE_PATH
#error "RPC_SERVER_SOURCE_PATH must be provided by tests/CMakeLists.txt"
#endif

#ifndef RPC_SERVER_HEADER_PATH
#error "RPC_SERVER_HEADER_PATH must be provided by tests/CMakeLists.txt"
#endif

#ifndef RPC_TYPES_HEADER_PATH
#error "RPC_TYPES_HEADER_PATH must be provided by tests/CMakeLists.txt"
#endif
#ifndef RPC_CLIENT_SOURCE_PATH
#error "RPC_CLIENT_SOURCE_PATH must be provided by tests/CMakeLists.txt"
#endif

#ifndef RPC_CLIENT_HEADER_PATH
#error "RPC_CLIENT_HEADER_PATH must be provided by tests/CMakeLists.txt"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(RpcServerLocalTransportTest, AlwaysUsesInsecureServerCredentials) {
    const auto source = readFile(RPC_SERVER_SOURCE_PATH);
    ASSERT_FALSE(source.empty()) << "Unable to read " << RPC_SERVER_SOURCE_PATH;

    EXPECT_NE(source.find("grpc::InsecureServerCredentials()"), std::string::npos);
    EXPECT_EQ(source.find("SslServerCredentials"), std::string::npos);
    EXPECT_EQ(source.find("setupSslCredentials"), std::string::npos);
    EXPECT_EQ(source.find("ssl_cert_path"), std::string::npos);
    EXPECT_EQ(source.find("ssl_key_path"), std::string::npos);
    EXPECT_EQ(source.find("SSL/TLS"), std::string::npos);
}

TEST(RpcServerLocalTransportTest, DoesNotExposeCertificateConfiguration) {
    const auto server_header = readFile(RPC_SERVER_HEADER_PATH);
    ASSERT_FALSE(server_header.empty()) << "Unable to read " << RPC_SERVER_HEADER_PATH;

    EXPECT_EQ(server_header.find("setupSslCredentials"), std::string::npos);
    EXPECT_EQ(server_header.find("ServerCredentials"), std::string::npos);
    EXPECT_EQ(server_header.find("ssl_cert_path"), std::string::npos);
    EXPECT_EQ(server_header.find("ssl_key_path"), std::string::npos);

    const auto common_header = readFile(RPC_TYPES_HEADER_PATH);

    ASSERT_FALSE(common_header.empty()) << "Unable to read " << RPC_TYPES_HEADER_PATH;
    EXPECT_EQ(common_header.find("ssl_cert_path"), std::string::npos);
    EXPECT_EQ(common_header.find("ssl_key_path"), std::string::npos);
    EXPECT_EQ(common_header.find("enable_ssl"), std::string::npos);
}

TEST(RpcServerLocalTransportTest, ClientAlwaysUsesInsecureChannelCredentials) {
    const auto client_source = readFile(RPC_CLIENT_SOURCE_PATH);
    ASSERT_FALSE(client_source.empty()) << "Unable to read " << RPC_CLIENT_SOURCE_PATH;

    EXPECT_NE(client_source.find("grpc::InsecureChannelCredentials()"), std::string::npos);
    EXPECT_EQ(client_source.find("grpc::SslCredentials"), std::string::npos);
    EXPECT_EQ(client_source.find("setupSslCredentials"), std::string::npos);
    EXPECT_EQ(client_source.find("enable_ssl"), std::string::npos);

    const auto client_header = readFile(RPC_CLIENT_HEADER_PATH);
    ASSERT_FALSE(client_header.empty()) << "Unable to read " << RPC_CLIENT_HEADER_PATH;
    EXPECT_EQ(client_header.find("setupSslCredentials"), std::string::npos);
}
