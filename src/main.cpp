#include "bridge/Bridge.hpp"
#include "config/ConfigLoader.hpp"
#include "dnp3/CommandDispatcher.hpp"
#include "dnp3/OutstationManager.hpp"
#include "grpc/BridgeServiceImpl.hpp"
#include "grpc/GrpcServer.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
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
        std::cerr << "Failed to load configuration: " << cfg_result.error() << "\n";
        return 1;
    }
    auto cfg = std::move(*cfg_result);

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

    // Run gRPC server on a separate thread so we can monitor the shutdown flag.
    std::jthread grpc_thread{[&server] {
        server.start(); // blocks until server.stop()
    }};

    // Poll for shutdown signal.
    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }

    std::cerr << "\nShutdown requested, cleaning up...\n";

    server.stop();
    bridge.stop();
    outstation.shutdown();

    return 0;
}
