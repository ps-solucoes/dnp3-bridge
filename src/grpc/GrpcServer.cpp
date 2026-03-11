#include "grpc/GrpcServer.hpp"
#include "grpc/BridgeServiceImpl.hpp"

#include <grpcpp/grpcpp.h>
#include <iostream>

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
        std::cerr << "[GrpcServer] Failed to start on " << listen_address_ << "\n";
        return;
    }

    std::cerr << "[GrpcServer] Listening on " << listen_address_ << "\n";
    server_->Wait(); // blocks
}

void GrpcServer::stop() {
    if (server_) {
        server_->Shutdown();
    }
}

} // namespace dnp3bridge::grpc
