#pragma once

#include <cstdint>
#include <string>

namespace dnp3bridge::config {

struct AppConfig {
    std::string  grpc_listen_address = "0.0.0.0:50051";
    std::string  dnp3_channel_host   = "0.0.0.0";
    std::uint16_t dnp3_channel_port  = 20000;
    std::uint16_t dnp3_local_address = 1024;
    std::uint16_t dnp3_remote_address = 1;
    std::string   log_level           = "info";
    std::uint32_t command_timeout_ms  = 3000;
};

} // namespace dnp3bridge::config
