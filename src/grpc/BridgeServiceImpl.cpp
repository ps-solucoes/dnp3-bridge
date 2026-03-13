#include "grpc/BridgeServiceImpl.hpp"
#include "bridge/Bridge.hpp"
#include "bridge/DataModel.hpp"
#include "dnp3/OutstationManager.hpp"
#include "dnp3/CommandDispatcher.hpp"

#include <opendnp3/gen/CommandStatus.h>

#include <chrono>
#include <spdlog/spdlog.h>
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
    spdlog::debug("UpdatePoints: {} analogs, {} binaries",
                  request->analogs_size(), request->binaries_size());

    for (const auto& a : request->analogs()) {
        spdlog::trace("  Analog update: index={} value={}", a.index(), a.value());
        bridge_.applyUpdate(bridge::AnalogUpdate{
            .index = static_cast<std::uint16_t>(a.index()),
            .value = a.value(),
        });
    }

    for (const auto& b : request->binaries()) {
        spdlog::trace("  Binary update: index={} value={}", b.index(), b.value());
        bridge_.applyUpdate(bridge::BinaryUpdate{
            .index = static_cast<std::uint16_t>(b.index()),
            .value = b.value(),
        });
    }

    auto now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    last_update_ms_.store(now_ms, std::memory_order_relaxed);

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

    response->set_last_update_timestamp_ms(
        last_update_ms_.load(std::memory_order_relaxed));

    return ::grpc::Status::OK;
}

::grpc::Status BridgeServiceImpl::StreamCommands(
    ::grpc::ServerContext* context,
    const dnp3bridge::v1::StreamCommandsRequest* /*request*/,
    ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* writer)
{
    auto token = dispatcher_.registerWriter(writer);
    spdlog::info("StreamCommands client connected");

    // Block until the client disconnects or the server is shutting down.
    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    spdlog::info("StreamCommands client disconnected");
    // StreamToken destructor will unregister the writer.
    return ::grpc::Status::OK;
}

::grpc::Status BridgeServiceImpl::RespondToCommand(
    ::grpc::ServerContext* /*context*/,
    const dnp3bridge::v1::CommandResponse* request,
    dnp3bridge::v1::UpdateResponse* response)
{
    spdlog::debug("RespondToCommand: command_id={} status={}",
                  request->command_id(), static_cast<int>(request->status()));

    auto status = toCommandStatus(request->status());
    bool ok = dispatcher_.fulfill(request->command_id(), status);

    if (!ok) {
        spdlog::warn("RespondToCommand: unknown command_id={}", request->command_id());
    }

    response->set_success(ok);
    response->set_message(ok ? "fulfilled" : "unknown command_id");
    return ::grpc::Status::OK;
}

} // namespace dnp3bridge::grpc
