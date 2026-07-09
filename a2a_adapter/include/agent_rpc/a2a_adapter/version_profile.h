#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace agent_rpc {
namespace a2a_adapter {

/**
 * @brief Version Profile (Batch 8: A2A Version Negotiation)
 *
 * Describes the protocol variant for a given A2A version string.
 * Different A2A versions may use different field names for the
 * same semantic concept (e.g. "kind" vs "type" for message part
 * discrimination), different status event formats, and different
 * sets of supported RPC methods.
 */
struct VersionProfile {
    /// Field name used for message part discrimination:
    /// A2A 1.0 uses "kind", A2A 1.1 uses "type".
    std::string part_field_name;

    /// Format string for status event field names.
    /// A2A 1.0 uses "status", A2A 1.1 uses "status".
    std::string status_event_format;

    /// Set of JSON-RPC methods supported by this version.
    std::vector<std::string> supported_methods;
};

/**
 * @brief Get the protocol profile for a given A2A version string.
 *
 * Returns the known profile for the requested version. If the version
 * is not recognized, falls back to the profile for A2A 1.0 (the
 * minimum common denominator).
 *
 * @param version The A2A protocol version string (e.g. "1.0", "1.1").
 * @return VersionProfile describing the protocol variant.
 */
inline VersionProfile getProfile(const std::string& version) {
    static const std::unordered_map<std::string, VersionProfile> map = {
        {"1.0", {"kind", "status", {"tasks/send", "tasks/get"}}},
        {"1.1", {"type", "status", {"tasks/send", "tasks/get", "tasks/cancel"}}},
    };

    auto it = map.find(version);
    if (it != map.end()) {
        return it->second;
    }

    // Fall back to A2A 1.0 for unknown versions
    return map.at("1.0");
}

} // namespace a2a_adapter
} // namespace agent_rpc
