#include "grpc/BridgeServiceImpl.hpp"
#include "bridge/Bridge.hpp"
#include "bridge/DataModel.hpp"
#include "dnp3/OutstationManager.hpp"
#include "dnp3/CommandDispatcher.hpp"

#include <opendnp3/gen/CommandStatus.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace dnp3bridge::grpc {

namespace {

opendnp3::CommandStatus toCommandStatus(dnp3bridge::v1::CommandResultStatus s) {
    switch (s) {
        case dnp3bridge::v1::COMMAND_RESULT_SUCCESS:         return opendnp3::CommandStatus::SUCCESS;
        case dnp3bridge::v1::COMMAND_RESULT_TIMEOUT:         return opendnp3::CommandStatus::TIMEOUT;
        case dnp3bridge::v1::COMMAND_RESULT_NO_SELECT:       return opendnp3::CommandStatus::NO_SELECT;
        case dnp3bridge::v1::COMMAND_RESULT_FORMAT_ERROR:    return opendnp3::CommandStatus::FORMAT_ERROR;
        case dnp3bridge::v1::COMMAND_RESULT_NOT_SUPPORTED:   return opendnp3::CommandStatus::NOT_SUPPORTED;
        case dnp3bridge::v1::COMMAND_RESULT_ALREADY_ACTIVE:  return opendnp3::CommandStatus::ALREADY_ACTIVE;
        case dnp3bridge::v1::COMMAND_RESULT_HARDWARE_ERROR:  return opendnp3::CommandStatus::HARDWARE_ERROR;
        case dnp3bridge::v1::COMMAND_RESULT_LOCAL:           return opendnp3::CommandStatus::LOCAL;
        case dnp3bridge::v1::COMMAND_RESULT_TOO_MANY_OPS:    return opendnp3::CommandStatus::TOO_MANY_OPS;
        case dnp3bridge::v1::COMMAND_RESULT_NOT_AUTHORIZED:  return opendnp3::CommandStatus::NOT_AUTHORIZED;
        case dnp3bridge::v1::COMMAND_RESULT_DOWNSTREAM_FAIL: return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
        default: return opendnp3::CommandStatus::UNDEFINED;
    }
}

} // anonymous namespace

BridgeServiceImpl::BridgeServiceImpl(bridge::Bridge& bridge,
                                     dnp3::OutstationManager& outstation,
                                     dnp3::CommandDispatcher& dispatcher)
    : bridge_{bridge}
    , outstation_{outstation}
    , dispatcher_{dispatcher}
{}

::grpc::Status BridgeServiceImpl::UpdatePoints(
    ::grpc::ServerContext* /*context*/,
    const dnp3bridge::v1::UpdateRequest* request,
    dnp3bridge::v1::UpdateResponse* response)
{
    for (const auto& a : request->analogs()) {
        bridge_.applyUpdate(bridge::AnalogUpdate{
            .index = static_cast<std::uint16_t>(a.index()),
            .value = a.value(),
        });
    }

    for (const auto& b : request->binaries()) {
        bridge_.applyUpdate(bridge::BinaryUpdate{
            .index = static_cast<std::uint16_t>(b.index()),
            .value = b.value(),
        });
    }

    for (const auto& c : request->counters()) {
        bridge_.applyUpdate(bridge::CounterUpdate{
            .index = static_cast<std::uint16_t>(c.index()),
            .value = static_cast<std::uint32_t>(c.value()),
        });
    }

    response->set_success(true);
    response->set_message("accepted");
    return ::grpc::Status::OK;
}

::grpc::Status BridgeServiceImpl::GetStatus(
    ::grpc::ServerContext* /*context*/,
    const dnp3bridge::v1::StatusRequest* /*request*/,
    dnp3bridge::v1::StatusResponse* response)
{
    if (outstation_.isConnected()) {
        response->set_state(dnp3bridge::v1::OUTSTATION_STATE_CONNECTED);
    } else {
        response->set_state(dnp3bridge::v1::OUTSTATION_STATE_DISCONNECTED);
    }

    // TODO: Track actual last-update timestamp.
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch())
                   .count();
    response->set_last_update_timestamp_ms(static_cast<std::uint64_t>(ms));

    return ::grpc::Status::OK;
}

::grpc::Status BridgeServiceImpl::StreamCommands(
    ::grpc::ServerContext* context,
    const dnp3bridge::v1::StreamCommandsRequest* /*request*/,
    ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* writer)
{
    auto token = dispatcher_.registerWriter(writer);

    // Block until the client disconnects or the server is shutting down.
    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    std::cerr << "[BridgeServiceImpl] StreamCommands client disconnected\n";
    // StreamToken destructor will unregister the writer.
    return ::grpc::Status::OK;
}

::grpc::Status BridgeServiceImpl::RespondToCommand(
    ::grpc::ServerContext* /*context*/,
    const dnp3bridge::v1::CommandResponse* request,
    dnp3bridge::v1::UpdateResponse* response)
{
    auto status = toCommandStatus(request->status());
    bool ok = dispatcher_.fulfill(request->command_id(), status);

    response->set_success(ok);
    response->set_message(ok ? "fulfilled" : "unknown command_id");
    return ::grpc::Status::OK;
}

} // namespace dnp3bridge::grpc
