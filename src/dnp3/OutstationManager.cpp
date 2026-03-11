#include "dnp3/OutstationManager.hpp"
#include "dnp3/ForwardingCommandHandler.hpp"

#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>

#include <iostream>

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

    channel_ = manager_->AddTCPServer(
        "server",
        opendnp3::levels::NORMAL,
        opendnp3::ServerAcceptMode::CloseExisting,
        opendnp3::IPEndpoint(cfg_.dnp3_channel_host, cfg_.dnp3_channel_port),
        nullptr
    );

    opendnp3::OutstationStackConfig stack_cfg(
        opendnp3::DatabaseConfig(10)
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

    std::cerr << "[OutstationManager] Started on "
              << cfg_.dnp3_channel_host << ":" << cfg_.dnp3_channel_port << "\n";
}

void OutstationManager::shutdown() {
    connected_ = false;
    outstation_.reset();
    channel_.reset();
    if (manager_) {
        manager_->Shutdown();
        manager_.reset();
        std::cerr << "[OutstationManager] Shutdown complete\n";
    }
}

void OutstationManager::updateAnalog(std::uint16_t index, double value) {
    if (!outstation_) return;
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Analog(value), index);
    outstation_->Apply(builder.Build());
}

void OutstationManager::updateBinary(std::uint16_t index, bool value) {
    if (!outstation_) return;
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Binary(value), index);
    outstation_->Apply(builder.Build());
}

void OutstationManager::updateCounter(std::uint16_t index, std::uint32_t value) {
    if (!outstation_) return;
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Counter(value), index);
    outstation_->Apply(builder.Build());
}

bool OutstationManager::isConnected() const {
    return connected_.load(std::memory_order_relaxed);
}

} // namespace dnp3bridge::dnp3
