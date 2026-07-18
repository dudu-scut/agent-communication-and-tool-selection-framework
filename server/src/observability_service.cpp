/**
 * @file observability_service.cpp
 * @brief ObservabilityService implementation — GetTraceDetail & GetCostReport
 *
 * Data flow:
 *   - Trace spans are written to Redis by AIQueryServiceImpl (Task #20) as
 *     JSON objects in list key  trace:{trace_id}:spans
 *   - Cost micro-dollar counters are written by CostTracker as
 *     string key  cost:{user_id}:{YYYY-MM-DD}
 *
 * This service reads those keys and serves the frontend Dashboard / Monitor.
 */

#include "agent_rpc/server/observability_service.h"
#include "agent_rpc/server/auth_interceptor.h"
#include "agent_rpc/common/logger.h"
#include "agent_rpc/common/cost_tracker.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace agent_rpc {
namespace server {

// ============================================================================
// Construction
// ============================================================================

ObservabilityServiceImpl::ObservabilityServiceImpl(common::RedisClient* redis_client)
    : redis_client_(redis_client) {
}

// ============================================================================
// Local helpers
// ============================================================================

namespace {

// Parse "YYYY-MM-DD" into a std::tm (zero-padded). Returns false on failure.
bool parseDate(const std::string& date_str, std::tm& tm) {
    if (date_str.size() != 10) return false;
    std::memset(&tm, 0, sizeof(tm));
    std::istringstream iss(date_str);
    iss >> std::get_time(&tm, "%Y-%m-%d");
    return !iss.fail();
}

// Format a std::tm back to "YYYY-MM-DD".
std::string formatDate(const std::tm& tm) {
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

// Advance tm by one day (normalises via mktime).
void advanceDay(std::tm& tm) {
    tm.tm_mday += 1;
    std::mktime(&tm);  // normalise
}

// Count days between two tm values (inclusive of start, exclusive of end).
int daysBetween(const std::tm& from, const std::tm& to) {
    std::time_t t_from = std::mktime(const_cast<std::tm*>(&from));
    std::time_t t_to   = std::mktime(const_cast<std::tm*>(&to));
    return static_cast<int>((t_to - t_from) / 86400);
}

} // anonymous namespace

// ============================================================================
// GetTraceDetail
// ============================================================================

grpc::Status ObservabilityServiceImpl::GetTraceDetail(
    grpc::ServerContext* context,
    const agent_communication::GetTraceDetailRequest* request,
    agent_communication::GetTraceDetailResponse* response) {

    (void)context;

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Invalid request or response");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Valid authentication token required");
    }

    const std::string& trace_id = request->trace_id();
    if (trace_id.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "trace_id is required");
    }

    LOG_INFO("GetTraceDetail for trace: " + trace_id);

    if (!redis_client_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "Redis not available");
    }

    // Read all span JSON objects from the Redis list
    std::string redis_key = "trace:" + trace_id + ":spans";
    std::vector<std::string> raw_spans;
    if (!redis_client_->lrange(redis_key, 0, -1, raw_spans) || raw_spans.empty()) {
        auto* status = response->mutable_status();
        status->set_code(-1);
        status->set_message("Trace not found: " + trace_id);
        return grpc::Status(grpc::StatusCode::NOT_FOUND,
                            "Trace not found: " + trace_id);
    }

    // Deserialise each JSON span into a proto TraceSpan
    std::ostringstream summary;
    bool first_span = true;

    for (const auto& raw : raw_spans) {
        try {
            auto j = nlohmann::json::parse(raw);
            auto* span = response->add_spans();

            span->set_trace_id(j.value("trace_id", trace_id));
            span->set_span_id(j.value("span_id", ""));
            span->set_parent_span_id(j.value("parent_span_id", ""));
            span->set_component(j.value("component", ""));

            // Wall-clock times (Unix ms)
            span->set_start_time(j.value("start_time", int64_t{0}));
            span->set_end_time(j.value("end_time", int64_t{0}));
            span->set_duration_ms(j.value("duration_ms", 0));

            span->set_status(j.value("status", "ok"));
            span->set_error_message(j.value("error_message", ""));
            span->set_metadata_json(j.value("metadata_json", ""));

            // Build human-readable summary: "component duration_ms -> ..."
            if (!first_span) summary << " \xe2\x86\x92 ";
            summary << span->component() << " " << span->duration_ms() << "ms";
            first_span = false;

        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Skipping malformed span JSON in trace " + trace_id +
                     ": " + std::string(e.what()));
        }
    }

    response->set_trace_summary(summary.str());

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    LOG_INFO("GetTraceDetail returned " + std::to_string(raw_spans.size()) +
             " spans for trace: " + trace_id);
    return grpc::Status::OK;
}

// ============================================================================
// GetCostReport
// ============================================================================

grpc::Status ObservabilityServiceImpl::GetCostReport(
    grpc::ServerContext* context,
    const agent_communication::GetCostReportRequest* request,
    agent_communication::GetCostReportResponse* response) {

    (void)context;

    if (!request || !response) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Invalid request or response");
    }
    if (!AuthInterceptor::isAuthenticated()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Valid authentication token required");
    }

    const std::string& user_id = request->user_id();
    if (user_id.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "user_id is required");
    }

    // Parse date range
    std::tm tm_start{}, tm_end{};
    if (!parseDate(request->start_date(), tm_start) ||
        !parseDate(request->end_date(),   tm_end)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Invalid date format; expected YYYY-MM-DD");
    }

    // Cap the range to 90 days to protect Redis from heavy scans
    int range_days = daysBetween(tm_start, tm_end);
    if (range_days < 0) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "start_date must be before or equal to end_date");
    }
    if (range_days > 90) {
        // Clamp end_date to start_date + 90 days
        tm_end = tm_start;
        for (int i = 0; i < 90; ++i) advanceDay(tm_end);
        range_days = 90;
    }

    LOG_INFO("GetCostReport for user=" + user_id +
             " from=" + request->start_date() +
             " to=" + formatDate(tm_end) +
             " (" + std::to_string(range_days + 1) + " days)");

    if (!redis_client_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "Redis not available");
    }

    double total_cost_usd = 0.0;

    // Iterate day by day (inclusive of end date)
    std::tm tm_cur = tm_start;
    for (int d = 0; d <= range_days; ++d) {
        std::string date_str = formatDate(tm_cur);
        std::string redis_key = "cost:" + user_id + ":" + date_str;

        std::string raw_value;
        if (redis_client_->get(redis_key, raw_value) && !raw_value.empty()) {
            try {
                // CostTracker stores micro-dollars (int64); convert to USD
                int64_t micro_dollars = std::stoll(raw_value);
                double cost_usd = static_cast<double>(micro_dollars) / 1'000'000.0;

                auto* record = response->add_records();
                record->set_user_id(user_id);
                record->set_date(date_str);
                record->set_total_cost_usd(cost_usd);
                // Token-level fields (total_prompt_tokens etc.) are not stored
                // in the simple cost key — left at default 0.

                total_cost_usd += cost_usd;
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse cost value for key " + redis_key +
                         ": " + std::string(e.what()));
            }
        }

        advanceDay(tm_cur);
    }

    response->set_total_cost_usd(total_cost_usd);

    auto* status = response->mutable_status();
    status->set_code(0);
    status->set_message("OK");

    LOG_INFO("GetCostReport returned " + std::to_string(response->records_size()) +
             " records, total=$" + std::to_string(total_cost_usd));
    return grpc::Status::OK;
}

} // namespace server
} // namespace agent_rpc
