#pragma once

#include <memory>
#include <string>

namespace grpc {
class Server;
}

namespace dnp3bridge::grpc {

class BridgeServiceImpl;

class GrpcServer {
public:
    GrpcServer(std::string listen_address, BridgeServiceImpl& service);
    ~GrpcServer();

    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;

    /// Build and start the gRPC server.  Blocks until stop() is called.
    void start();

    /// Initiate graceful shutdown.
    void stop();

private:
    std::string                        listen_address_;
    BridgeServiceImpl&                 service_;
    std::unique_ptr<::grpc::Server>    server_;
};

} // namespace dnp3bridge::grpc
