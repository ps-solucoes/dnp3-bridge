#pragma once

#include "bridge/DataModel.hpp"
#include "config/AppConfig.hpp"

#include <opendnp3/DNP3Manager.h>
#include <opendnp3/outstation/IOutstation.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>

namespace dnp3bridge::dnp3 {

class CommandDispatcher; // forward declaration

/// Checks every enum-valued string in the point database against the names the
/// DNP3 layer accepts. Returns a message naming the offending key on the first
/// bad value, so a typo fails startup instead of silently changing the wire
/// format the SCADA sees.
[[nodiscard]] auto validatePointDatabase(const config::PointDatabaseConfig& db)
    -> std::expected<void, std::string>;

/// Manages a single DNP3 outstation, providing methods to push point updates.
class OutstationManager {
public:
    OutstationManager(const config::AppConfig& cfg, CommandDispatcher& dispatcher);
    ~OutstationManager();

    OutstationManager(const OutstationManager&) = delete;
    OutstationManager& operator=(const OutstationManager&) = delete;

    void start();
    void shutdown();

    void updateAnalog(std::uint16_t index, double value, bridge::Quality quality);
    void updateBinary(std::uint16_t index, bool value, bridge::Quality quality);

    [[nodiscard]] bool isConnected() const;

private:
    /// Last value we generated an event for, plus the flags last written.
    /// Events are decided here rather than by opendnp3's EventMode::Detect so
    /// that a quality change can never generate one (REQ-11) nor short-circuit
    /// the deadband (REQ-08) -- opendnp3's IsEvent() returns true on any flags
    /// difference, before it ever looks at the deadband.
    template <typename T>
    struct PointState {
        T             last_evented{};
        std::uint8_t  flags = 0;
        bool          seen  = false;
    };

    config::AppConfig                         cfg_;
    CommandDispatcher&                        dispatcher_;
    std::unique_ptr<opendnp3::DNP3Manager>    manager_;
    std::shared_ptr<opendnp3::IChannel>       channel_;
    std::shared_ptr<opendnp3::IOutstation>    outstation_;
    std::atomic<bool>                         connected_{false};

    // Written only from the Bridge flush thread.
    std::unordered_map<std::uint16_t, double>            analog_deadband_;
    std::unordered_map<std::uint16_t, PointState<double>> analog_state_;
    std::unordered_map<std::uint16_t, PointState<bool>>   binary_state_;
};

} // namespace dnp3bridge::dnp3
