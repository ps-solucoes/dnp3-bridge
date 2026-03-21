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

void parsePointConfigs(const nlohmann::json& arr, std::vector<PointConfig>& out) {
    for (const auto& entry : arr) {
        PointConfig pc;
        if (entry.contains("range")) {
            auto range = entry["range"];
            pc.start = range[0].get<std::uint16_t>();
            pc.end   = range[1].get<std::uint16_t>();
        } else if (entry.contains("index")) {
            pc.start = entry["index"].get<std::uint16_t>();
            pc.end   = pc.start;
        }
        if (entry.contains("class"))            pc.event_class      = entry["class"].get<std::string>();
        if (entry.contains("deadband"))         pc.deadband          = entry["deadband"].get<double>();
        if (entry.contains("static_variation")) pc.static_variation  = entry["static_variation"].get<std::string>();
        if (entry.contains("event_variation"))  pc.event_variation   = entry["event_variation"].get<std::string>();
        out.push_back(std::move(pc));
    }
}

void parseEventBuffer(const nlohmann::json& j, EventBufferConfig& eb) {
    if (j.contains("max_binary_events"))               eb.max_binary_events               = j["max_binary_events"].get<std::uint16_t>();
    if (j.contains("max_double_binary_events"))        eb.max_double_binary_events         = j["max_double_binary_events"].get<std::uint16_t>();
    if (j.contains("max_analog_events"))               eb.max_analog_events               = j["max_analog_events"].get<std::uint16_t>();
    if (j.contains("max_counter_events"))              eb.max_counter_events              = j["max_counter_events"].get<std::uint16_t>();
    if (j.contains("max_frozen_counter_events"))       eb.max_frozen_counter_events       = j["max_frozen_counter_events"].get<std::uint16_t>();
    if (j.contains("max_binary_output_status_events")) eb.max_binary_output_status_events = j["max_binary_output_status_events"].get<std::uint16_t>();
    if (j.contains("max_analog_output_status_events")) eb.max_analog_output_status_events = j["max_analog_output_status_events"].get<std::uint16_t>();
    if (j.contains("max_octet_string_events"))         eb.max_octet_string_events         = j["max_octet_string_events"].get<std::uint16_t>();
}

void parseUnsolicited(const nlohmann::json& j, UnsolicitedConfig& us) {
    if (j.contains("enabled"))    us.enabled = j["enabled"].get<bool>();
    if (j.contains("class_mask")) {
        us.class_mask.clear();
        for (const auto& c : j["class_mask"]) {
            us.class_mask.push_back(c.get<std::string>());
        }
    }
}

void parsePointDatabase(const nlohmann::json& j, PointDatabaseConfig& db) {
    if (j.contains("binary_input"))          parsePointConfigs(j["binary_input"],          db.binary_input);
    if (j.contains("binary_output_status"))  parsePointConfigs(j["binary_output_status"],  db.binary_output_status);
    if (j.contains("analog_input"))          parsePointConfigs(j["analog_input"],          db.analog_input);
    if (j.contains("analog_output_status"))  parsePointConfigs(j["analog_output_status"],  db.analog_output_status);
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
    if (j.contains("log_file"))             cfg.log_file            = j["log_file"].get<std::string>();
    if (j.contains("log_max_size_mb"))      cfg.log_max_size_mb     = j["log_max_size_mb"].get<std::uint32_t>();
    if (j.contains("log_max_files"))        cfg.log_max_files       = j["log_max_files"].get<std::uint32_t>();
    if (j.contains("command_timeout_ms"))   cfg.command_timeout_ms  = j["command_timeout_ms"].get<std::uint32_t>();
    if (j.contains("command_mode"))        cfg.command_mode        = j["command_mode"].get<std::string>();

    if (j.contains("event_buffer"))    parseEventBuffer(j["event_buffer"], cfg.event_buffer);
    if (j.contains("unsolicited"))     parseUnsolicited(j["unsolicited"], cfg.unsolicited);
    if (j.contains("point_database"))  parsePointDatabase(j["point_database"], cfg.point_database);

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
    cfg.log_file            = env_or("DNP3_BRIDGE_LOG_FILE", cfg.log_file);
    cfg.log_max_size_mb     = env_uint32("DNP3_BRIDGE_LOG_MAX_SIZE_MB", cfg.log_max_size_mb);
    cfg.log_max_files       = env_uint32("DNP3_BRIDGE_LOG_MAX_FILES", cfg.log_max_files);
    cfg.command_timeout_ms  = env_uint32("DNP3_BRIDGE_COMMAND_TIMEOUT_MS", cfg.command_timeout_ms);
    cfg.command_mode        = env_or("DNP3_BRIDGE_COMMAND_MODE", cfg.command_mode);

    return cfg;
}

} // namespace dnp3bridge::config
