#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dnp3bridge::config {

struct PointConfig {
    std::uint16_t start = 0;
    std::uint16_t end   = 0;       // inclusive; start==end means single point
    std::string   event_class;     // "class0", "class1", "class2", "class3"
    std::optional<double> deadband;  // unset = leave as-is; only meaningful for analog types
    std::string   static_variation; // e.g. "Group30Var2"; empty = opendnp3 default
    std::string   event_variation;  // e.g. "Group32Var2"; empty = opendnp3 default
};

struct PointDatabaseConfig {
    std::vector<PointConfig> binary_input;
    std::vector<PointConfig> binary_output_status;
    std::vector<PointConfig> analog_input;
    std::vector<PointConfig> analog_output_status;

    [[nodiscard]] bool empty() const {
        return binary_input.empty() && binary_output_status.empty()
            && analog_input.empty() && analog_output_status.empty();
    }
};

struct EventBufferConfig {
    std::uint16_t max_binary_events               = 50;
    std::uint16_t max_double_binary_events         = 0;
    std::uint16_t max_analog_events               = 50;
    std::uint16_t max_counter_events              = 0;
    std::uint16_t max_frozen_counter_events       = 0;
    std::uint16_t max_binary_output_status_events = 0;
    std::uint16_t max_analog_output_status_events = 0;
    std::uint16_t max_octet_string_events         = 0;
};

struct UnsolicitedConfig {
    bool enabled = true;
    std::vector<std::string> class_mask = {"class1", "class2"};
};

struct AppConfig {
    std::string   grpc_listen_address = "0.0.0.0:50051";
    std::string   dnp3_channel_host   = "0.0.0.0";
    std::uint16_t dnp3_channel_port   = 20000;
    std::uint16_t dnp3_local_address  = 1024;
    std::uint16_t dnp3_remote_address = 1;
    std::string   log_level           = "info";
    std::string   log_file;  // empty = no file logging
    std::uint32_t log_max_size_mb     = 5;
    std::uint32_t log_max_files       = 3;
    std::uint32_t command_timeout_ms  = 3000;
    std::string   command_mode        = "direct_operate"; // "direct_operate" | "select_before_operate"

    EventBufferConfig    event_buffer;
    UnsolicitedConfig    unsolicited;
    PointDatabaseConfig  point_database;
};

} // namespace dnp3bridge::config
