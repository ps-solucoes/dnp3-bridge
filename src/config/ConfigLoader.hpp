#pragma once

#include "config/AppConfig.hpp"

#include <expected>
#include <optional>
#include <string>

namespace dnp3bridge::config {

class ConfigLoader {
public:
    /// Load configuration from a JSON file (if path given), then overlay
    /// environment variables.  Env vars always take precedence over the file.
    static auto load(std::optional<std::string> path = std::nullopt)
        -> std::expected<AppConfig, std::string>;
};

} // namespace dnp3bridge::config
