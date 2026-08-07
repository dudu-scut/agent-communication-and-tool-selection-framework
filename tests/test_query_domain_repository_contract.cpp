#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#ifndef NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER
#error "NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER must point at query_domain_repository.h"
#endif

#ifndef NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE
#error "NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE must point at query_domain_repository.cpp"
#endif

namespace {

std::string readFile(const char* path) {
    std::ifstream source{path, std::ios::binary};
    EXPECT_TRUE(source.is_open()) << "cannot read " << path;
    return {std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

TEST(QueryDomainRepositoryContractTest, HeaderExposesPostgresBackedDomainOperations) {
    const std::string header = readFile(NEXUSAI_QUERY_DOMAIN_REPOSITORY_HEADER);
    ASSERT_FALSE(header.empty());
    EXPECT_NE(header.find("PostgresStore"), std::string::npos);
    EXPECT_NE(header.find("class QueryDomainRepository"), std::string::npos);

    for (const std::string operation : {
             "createConversation", "getConversationById", "listConversations", "appendMessage",
             "listMessages", "createQueryLog", "getQueryLogById", "createTrace", "getTraceById",
             "appendTokenUsageLedger", "listTokenUsageLedgerByOwner", "createFeedback",
             "getRouteQuality", "upsertRouteQuality"}) {
        EXPECT_NE(header.find(operation), std::string::npos) << operation;
    }
}

TEST(QueryDomainRepositoryContractTest, SourceUsesBoundParametersAndExplicitOwnerFilters) {
    const std::string source = readFile(NEXUSAI_QUERY_DOMAIN_REPOSITORY_SOURCE);
    ASSERT_FALSE(source.empty());

    EXPECT_NE(source.find("execParams"), std::string::npos);
    EXPECT_NE(source.find("$1"), std::string::npos);
    EXPECT_NE(source.find("$2"), std::string::npos);
    EXPECT_NE(source.find("$3"), std::string::npos);
    EXPECT_EQ(source.find("popen"), std::string::npos);
    EXPECT_EQ(uppercase(source).find("REDIS"), std::string::npos);

    // Every read is tenant scoped. The repository has seven read methods:
    // conversation get/list, message list, query-log get, trace get, token list,
    // and route-quality get.
    const std::regex owner_filter{R"(WHERE\s+[^;]*OWNER_ID\s*=\s*\$[0-9])",
                                  std::regex::icase};
    EXPECT_GE(std::distance(std::sregex_iterator(source.begin(), source.end(), owner_filter),
                            std::sregex_iterator()),
              7);

    // Message sequence conflicts are the sole DO NOTHING idempotency path.
    const std::string upper_source = uppercase(source);
    EXPECT_NE(upper_source.find("ON CONFLICT (OWNER_ID, CONVERSATION_ID, SEQUENCE_NO) DO NOTHING"),
              std::string::npos);
    const std::regex do_nothing_pattern{"ON CONFLICT[^;]*DO NOTHING"};
    EXPECT_EQ(std::distance(std::sregex_iterator(upper_source.begin(), upper_source.end(),
                                                 do_nothing_pattern),
                            std::sregex_iterator()),
              1);
}

}  // namespace
