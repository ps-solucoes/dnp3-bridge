#include "dnp3/OutstationManager.hpp"
#include "dnp3/ForwardingCommandHandler.hpp"

#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>

#include <spdlog/spdlog.h>

namespace dnp3bridge::dnp3 {

OutstationManager::OutstationManager(const config::AppConfig& cfg, CommandDispatcher& dispatcher)
    : cfg_{cfg}
    , dispatcher_{dispatcher}
{}

OutstationManager::~OutstationManager() {
    shutdown();
}

void OutstationManager::start() {
    manager_ = std::make_unique<opendnp3::DNP3Manager>(2);
    spdlog::debug("DNP3Manager created with 2 threads");

    channel_ = manager_->AddTCPServer(
        "server",
        opendnp3::levels::NORMAL,
        opendnp3::ServerAcceptMode::CloseExisting,
        opendnp3::IPEndpoint(cfg_.dnp3_channel_host, cfg_.dnp3_channel_port),
        nullptr
    );
    spdlog::debug("TCP server channel created");

    opendnp3::DatabaseConfig db_config;

    // Binary Input: 11 points (indices 0-10), all Class 1
    for (uint16_t i = 0; i <= 10; ++i) {
        auto& pt = db_config.binary_input[i];
        pt.clazz = opendnp3::PointClass::Class1;
    }

    // Binary Output Status: 20 points (indices 0-19)
    //   0-11: Class 1, 12-19: Class 0 (static only, no event reporting)
    for (uint16_t i = 0; i <= 11; ++i) {
        auto& pt = db_config.binary_output_status[i];
        pt.clazz = opendnp3::PointClass::Class1;
    }
    for (uint16_t i = 12; i <= 19; ++i) {
        auto& pt = db_config.binary_output_status[i];
        pt.clazz = opendnp3::PointClass::Class0;
    }

    // Analog Input: 21 points (indices 0-20), all Class 2, 16-bit integer
    for (uint16_t i = 0; i <= 20; ++i) {
        auto& pt = db_config.analog_input[i];
        pt.clazz = opendnp3::PointClass::Class2;
        pt.svariation = opendnp3::StaticAnalogVariation::Group30Var2;
        pt.evariation = opendnp3::EventAnalogVariation::Group32Var2;
    }

    // Analog Output Status: 5 points (indices 0-4), all Class 2, 16-bit integer
    for (uint16_t i = 0; i <= 4; ++i) {
        auto& pt = db_config.analog_output_status[i];
        pt.clazz = opendnp3::PointClass::Class2;
        pt.svariation = opendnp3::StaticAnalogOutputStatusVariation::Group40Var2;
        pt.evariation = opendnp3::EventAnalogOutputStatusVariation::Group42Var2;
    }

    // All other types (double binary, counter, frozen counter,
    // time and interval, octet string) remain empty.

    spdlog::debug("Outstation database configured: 11 BI, 20 BO, 21 AI, 5 AO");

    opendnp3::OutstationStackConfig stack_cfg(std::move(db_config));
    stack_cfg.outstation.eventBufferConfig = opendnp3::EventBufferConfig(
        11,  // maxBinaryEvents (11 binary inputs)
        0,   // maxDoubleBinaryEvents
        21,  // maxAnalogEvents (21 analog inputs)
        0,   // maxCounterEvents
        0,   // maxFrozenCounterEvents
        20,  // maxBinaryOutputStatusEvents (20 binary outputs)
        5,   // maxAnalogOutputStatusEvents (5 analog outputs)
        0    // maxOctetStringEvents
    );
    stack_cfg.outstation.params.allowUnsolicited = true;
    stack_cfg.link.LocalAddr  = cfg_.dnp3_local_address;
    stack_cfg.link.RemoteAddr = cfg_.dnp3_remote_address;

    outstation_ = channel_->AddOutstation(
        "outstation",
        ForwardingCommandHandler::Create(dispatcher_),
        opendnp3::DefaultOutstationApplication::Create(),
        stack_cfg
    );

    outstation_->Enable();
    connected_ = true;

    spdlog::info("Outstation started on {}:{}", cfg_.dnp3_channel_host, cfg_.dnp3_channel_port);
}

void OutstationManager::shutdown() {
    connected_ = false;
    outstation_.reset();
    channel_.reset();
    if (manager_) {
        manager_->Shutdown();
        manager_.reset();
        spdlog::info("Outstation shutdown complete");
    }
}

void OutstationManager::updateAnalog(std::uint16_t index, double value) {
    if (!outstation_) return;
    spdlog::trace("Update analog[{}] = {}", index, value);
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Analog(value), index);
    outstation_->Apply(builder.Build());
}

void OutstationManager::updateBinary(std::uint16_t index, bool value) {
    if (!outstation_) return;
    spdlog::trace("Update binary[{}] = {}", index, value);
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Binary(value), index);
    outstation_->Apply(builder.Build());
}

bool OutstationManager::isConnected() const {
    return connected_.load(std::memory_order_relaxed);
}

} // namespace dnp3bridge::dnp3
