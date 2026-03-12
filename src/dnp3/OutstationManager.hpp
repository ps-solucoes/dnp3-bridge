#pragma once

#include "config/AppConfig.hpp"

#include <opendnp3/DNP3Manager.h>
#include <opendnp3/outstation/IOutstation.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace dnp3bridge::dnp3 {

class CommandDispatcher; // forward declaration

/// Manages a single DNP3 outstation, providing methods to push point updates.
class OutstationManager {
public:
    OutstationManager(const config::AppConfig& cfg, CommandDispatcher& dispatcher);
    ~OutstationManager();

    OutstationManager(const OutstationManager&) = delete;
    OutstationManager& operator=(const OutstationManager&) = delete;

    void start();
    void shutdown();

    void updateAnalog(std::uint16_t index, double value);
    void updateBinary(std::uint16_t index, bool value);

    [[nodiscard]] bool isConnected() const;

private:
    config::AppConfig                         cfg_;
    CommandDispatcher&                        dispatcher_;
    std::unique_ptr<opendnp3::DNP3Manager>    manager_;
    std::shared_ptr<opendnp3::IChannel>       channel_;
    std::shared_ptr<opendnp3::IOutstation>    outstation_;
    std::atomic<bool>                         connected_{false};
};

} // namespace dnp3bridge::dnp3
