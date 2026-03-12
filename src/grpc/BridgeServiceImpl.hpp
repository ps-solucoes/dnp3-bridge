#pragma once

#include "dnp3bridge.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>

namespace dnp3bridge::bridge {
class Bridge;
}
namespace dnp3bridge::dnp3 {
class OutstationManager;
class CommandDispatcher;
}

namespace dnp3bridge::grpc {

class BridgeServiceImpl final : public dnp3bridge::v1::BridgeService::Service {
public:
    BridgeServiceImpl(bridge::Bridge& bridge,
                      dnp3::OutstationManager& outstation,
                      dnp3::CommandDispatcher& dispatcher);

    ::grpc::Status UpdatePoints(
        ::grpc::ServerContext* context,
        const dnp3bridge::v1::UpdateRequest* request,
        dnp3bridge::v1::UpdateResponse* response) override;

    ::grpc::Status GetStatus(
        ::grpc::ServerContext* context,
        const dnp3bridge::v1::StatusRequest* request,
        dnp3bridge::v1::StatusResponse* response) override;

    ::grpc::Status StreamCommands(
        ::grpc::ServerContext* context,
        const dnp3bridge::v1::StreamCommandsRequest* request,
        ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* writer) override;

    ::grpc::Status RespondToCommand(
        ::grpc::ServerContext* context,
        const dnp3bridge::v1::CommandResponse* request,
        dnp3bridge::v1::UpdateResponse* response) override;

private:
    bridge::Bridge&            bridge_;
    dnp3::OutstationManager&   outstation_;
    dnp3::CommandDispatcher&   dispatcher_;
    std::atomic<uint64_t>      last_update_ms_{0};
};

} // namespace dnp3bridge::grpc
