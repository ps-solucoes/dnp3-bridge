#include "bridge/Bridge.hpp"
#include "config/ConfigLoader.hpp"
#include "dnp3/CommandDispatcher.hpp"
#include "dnp3/OutstationManager.hpp"
#include "grpc/BridgeServiceImpl.hpp"
#include "grpc/GrpcServer.hpp"

#include <atomic>
#include <csignal>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <optional>
#include <string>

namespace {

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int /*sig*/) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Optional config path from command-line argument.
    std::optional<std::string> config_path;
    if (argc >= 2) {
        config_path = argv[1];
    }

    // Load configuration.
    auto cfg_result = dnp3bridge::config::ConfigLoader::load(config_path);
    if (!cfg_result) {
        fmt::print(stderr, "Failed to load configuration: {}\n", cfg_result.error());
        return 1;
    }
    auto cfg = std::move(*cfg_result);

    if (auto valid = dnp3bridge::dnp3::validatePointDatabase(cfg.point_database); !valid) {
        fmt::print(stderr, "Failed to load configuration: {}\n", valid.error());
        return 1;
    }

    // Configure logging.
    auto log_level = spdlog::level::from_str(cfg.log_level);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

    if (!cfg.log_file.empty()) {
        auto max_size = static_cast<std::size_t>(cfg.log_max_size_mb) * 1024 * 1024;
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.log_file, max_size, cfg.log_max_files));
    }

    auto logger = std::make_shared<spdlog::logger>("dnp3-bridge", sinks.begin(), sinks.end());
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    spdlog::set_default_logger(logger);

    spdlog::info("dnp3-bridge v{} starting", "0.1.0");
    if (config_path) {
        spdlog::info("Config file: {}", *config_path);
    } else {
        spdlog::warn("No config file argument -- using built-in defaults plus environment "
                     "overrides; DNP3 point_database (classes, deadbands, variations) is JSON-only "
                     "and cannot be set via environment");
    }
    spdlog::info("gRPC address: {}", cfg.grpc_listen_address);
    spdlog::info("DNP3 endpoint: {}:{}", cfg.dnp3_channel_host, cfg.dnp3_channel_port);
    spdlog::info("DNP3 addresses: local={}, remote={}", cfg.dnp3_local_address, cfg.dnp3_remote_address);
    spdlog::info("Command timeout: {}ms", cfg.command_timeout_ms);
    spdlog::info("Log level: {}", cfg.log_level);
    if (!cfg.log_file.empty()) {
        spdlog::info("Log file: {} (max {}MB, {} rotated files)",
                     cfg.log_file, cfg.log_max_size_mb, cfg.log_max_files);
    }

    // Ignore SIGPIPE — gRPC stream writes to a disconnected client must not
    // kill the process.  gRPC normally masks this, but the opendnp3 threads
    // that call dispatch() may not inherit that mask.
    std::signal(SIGPIPE, SIG_IGN);

    // Install signal handlers.
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Construct components.
    dnp3bridge::dnp3::CommandDispatcher  dispatcher{std::chrono::milliseconds{cfg.command_timeout_ms}};
    dnp3bridge::dnp3::OutstationManager  outstation{cfg, dispatcher};
    dnp3bridge::bridge::Bridge           bridge{outstation};
    dnp3bridge::grpc::BridgeServiceImpl  service{bridge, outstation, dispatcher};
    dnp3bridge::grpc::GrpcServer         server{cfg.grpc_listen_address, service};

    // Start DNP3 outstation and bridge flush thread.
    outstation.start();
    bridge.start();
    spdlog::info("Bridge flush thread started");

    // Run gRPC server on a separate thread so we can monitor the shutdown flag.
    std::jthread grpc_thread{[&server] {
        server.start(); // blocks until server.stop()
    }};
    spdlog::info("gRPC server thread started");

    // Poll for shutdown signal.
    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }

    spdlog::info("Shutdown requested, cleaning up...");

    server.stop();
    bridge.stop();
    outstation.shutdown();

    spdlog::info("Shutdown complete");
    return 0;
}
