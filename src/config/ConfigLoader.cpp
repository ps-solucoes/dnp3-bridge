#include "config/ConfigLoader.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

namespace dnp3bridge::config {

namespace {

auto env_or(const char* name, std::string fallback) -> std::string {
    const char* val = std::getenv(name);
    return val ? std::string{val} : std::move(fallback);
}

auto env_uint16(const char* name, std::uint16_t fallback) -> std::uint16_t {
    const char* val = std::getenv(name);
    if (!val) return fallback;
    try {
        return static_cast<std::uint16_t>(std::stoul(val));
    } catch (...) {
        return fallback;
    }
}

auto env_uint32(const char* name, std::uint32_t fallback) -> std::uint32_t {
    const char* val = std::getenv(name);
    if (!val) return fallback;
    try {
        return static_cast<std::uint32_t>(std::stoul(val));
    } catch (...) {
        return fallback;
    }
}

auto load_from_json(const std::string& path, AppConfig& cfg) -> std::expected<void, std::string> {
    std::ifstream file{path};
    if (!file.is_open()) {
        return std::unexpected{"cannot open config file: " + path};
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected{std::string{"JSON parse error: "} + e.what()};
    }

    if (j.contains("grpc_listen_address"))  cfg.grpc_listen_address = j["grpc_listen_address"].get<std::string>();
    if (j.contains("dnp3_channel_host"))    cfg.dnp3_channel_host   = j["dnp3_channel_host"].get<std::string>();
    if (j.contains("dnp3_channel_port"))    cfg.dnp3_channel_port   = j["dnp3_channel_port"].get<std::uint16_t>();
    if (j.contains("dnp3_local_address"))   cfg.dnp3_local_address  = j["dnp3_local_address"].get<std::uint16_t>();
    if (j.contains("dnp3_remote_address"))  cfg.dnp3_remote_address = j["dnp3_remote_address"].get<std::uint16_t>();
    if (j.contains("log_level"))            cfg.log_level           = j["log_level"].get<std::string>();
    if (j.contains("command_timeout_ms"))   cfg.command_timeout_ms  = j["command_timeout_ms"].get<std::uint32_t>();

    return {};
}

} // anonymous namespace

auto ConfigLoader::load(std::optional<std::string> path)
    -> std::expected<AppConfig, std::string>
{
    AppConfig cfg{};

    // Layer 1: JSON config file (if provided).
    if (path.has_value()) {
        auto result = load_from_json(*path, cfg);
        if (!result) {
            return std::unexpected{result.error()};
        }
    }

    // Layer 2: Environment variables override file values.
    cfg.grpc_listen_address = env_or("DNP3_BRIDGE_GRPC_ADDRESS", cfg.grpc_listen_address);
    cfg.dnp3_channel_host   = env_or("DNP3_BRIDGE_DNP3_HOST",    cfg.dnp3_channel_host);
    cfg.dnp3_channel_port   = env_uint16("DNP3_BRIDGE_DNP3_PORT", cfg.dnp3_channel_port);
    cfg.dnp3_local_address  = env_uint16("DNP3_BRIDGE_DNP3_LOCAL_ADDR",  cfg.dnp3_local_address);
    cfg.dnp3_remote_address = env_uint16("DNP3_BRIDGE_DNP3_REMOTE_ADDR", cfg.dnp3_remote_address);
    cfg.log_level           = env_or("DNP3_BRIDGE_LOG_LEVEL", cfg.log_level);
    cfg.command_timeout_ms  = env_uint32("DNP3_BRIDGE_COMMAND_TIMEOUT_MS", cfg.command_timeout_ms);

    return cfg;
}

} // namespace dnp3bridge::config
