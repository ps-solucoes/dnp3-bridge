#include "grpc/GrpcServer.hpp"
#include "grpc/BridgeServiceImpl.hpp"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

namespace dnp3bridge::grpc {

GrpcServer::GrpcServer(std::string listen_address, BridgeServiceImpl& service)
    : listen_address_{std::move(listen_address)}
    , service_{service}
{}

GrpcServer::~GrpcServer() {
    stop();
}

void GrpcServer::start() {
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_address_, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);

    server_ = builder.BuildAndStart();
    if (!server_) {
        spdlog::critical("gRPC server failed to start on {}", listen_address_);
        return;
    }

    spdlog::info("gRPC server listening on {}", listen_address_);
    server_->Wait(); // blocks
}

void GrpcServer::stop() {
    if (server_) {
        spdlog::info("gRPC server shutting down");
        server_->Shutdown();
    }
}

} // namespace dnp3bridge::grpc
