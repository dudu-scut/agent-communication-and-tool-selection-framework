/**
 * @file env_loader.h
 * @brief .env file loader — reads key=value pairs and injects into environment
 *
 * Usage:
 *   #include "agent_rpc/common/env_loader.h"
 *   agent_rpc::common::loadEnvFile(".env");  // call early in main()
 *
 * Behavior:
 *   - Lines starting with '#' are comments
 *   - Empty lines are skipped
 *   - Format: KEY=VALUE (no quotes needed, but both ' and " are stripped)
 *   - Existing env vars are NOT overwritten (setenv overwrite=false)
 *   - File not found is silently ignored (not an error)
 */

#pragma once

#include <cstdlib>
#include <fstream>
#include <string>

namespace agent_rpc {
namespace common {

inline std::string trimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

/**
 * @brief Read an environment variable with a fallback default value.
 * Safe to use in struct field initializers.
 */
inline std::string envOrDefault(const char* name, const std::string& fallback) {
    const char* val = std::getenv(name);
    return (val && val[0] != '\0') ? std::string(val) : fallback;
}

inline float envOrFloat(const char* name, float fallback) {
    const char* val = std::getenv(name);
    if (val && val[0] != '\0') {
        try { return std::stof(val); } catch (...) {}
    }
    return fallback;
}

inline int envOrInt(const char* name, int fallback) {
    const char* val = std::getenv(name);
    if (val && val[0] != '\0') {
        try { return std::stoi(val); } catch (...) {}
    }
    return fallback;
}

/**
 * @brief Load a .env file and inject key=value pairs into environment.
 * @param path Path to the .env file (default: ".env" in current directory)
 * @return Number of variables loaded
 */
inline int loadEnvFile(const std::string& path = ".env") {
    std::ifstream file(path);
    if (!file.is_open()) return 0;

    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        line = trimWhitespace(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Find the '=' separator
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trimWhitespace(line.substr(0, eq));
        std::string value = stripQuotes(trimWhitespace(line.substr(eq + 1)));

        if (key.empty()) continue;

        // Set env var only if not already set (don't override shell exports)
#ifdef _WIN32
        // On Windows, _putenv_s always overwrites; check first
        char* existing = nullptr;
        size_t len = 0;
        _dupenv_s(&existing, &len, key.c_str());
        if (existing == nullptr) {
            _putenv_s(key.c_str(), value.c_str());
            ++count;
        }
        free(existing);
#else
        if (setenv(key.c_str(), value.c_str(), 0) == 0) {
            ++count;
        }
#endif
    }

    return count;
}

} // namespace common
} // namespace agent_rpc
