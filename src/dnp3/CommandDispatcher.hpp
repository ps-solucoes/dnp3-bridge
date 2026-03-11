#pragma once

#include "dnp3bridge.grpc.pb.h"

#include <opendnp3/gen/CommandStatus.h>

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <unordered_map>

namespace dnp3bridge::dnp3 {

class CommandDispatcher {
public:
    explicit CommandDispatcher(std::chrono::milliseconds timeout);

    /// RAII token -- when destroyed, unregisters the writer.
    class StreamToken {
    public:
        ~StreamToken();
        StreamToken(StreamToken&& other) noexcept;
        StreamToken& operator=(StreamToken&& other) noexcept;

        StreamToken(const StreamToken&) = delete;
        StreamToken& operator=(const StreamToken&) = delete;

    private:
        friend class CommandDispatcher;
        explicit StreamToken(CommandDispatcher* dispatcher);
        CommandDispatcher* dispatcher_;
    };

    /// Called by BridgeServiceImpl when Python opens StreamCommands.
    [[nodiscard]] StreamToken registerWriter(
        ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* writer);

    /// Called by ForwardingCommandHandler from opendnp3 thread.
    /// Blocks until Python responds or timeout. Returns CommandStatus.
    opendnp3::CommandStatus dispatch(dnp3bridge::v1::CommandRequest request);

    /// Called by BridgeServiceImpl when Python calls RespondToCommand.
    bool fulfill(uint64_t command_id, opendnp3::CommandStatus status);

private:
    std::chrono::milliseconds timeout_;
    std::atomic<uint64_t> next_id_{1};

    std::mutex writer_mutex_;
    ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* active_writer_{nullptr};

    std::mutex pending_mutex_;
    std::unordered_map<uint64_t, std::promise<opendnp3::CommandStatus>> pending_;
};

} // namespace dnp3bridge::dnp3
