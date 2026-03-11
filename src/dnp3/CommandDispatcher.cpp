#include "dnp3/CommandDispatcher.hpp"

#include <iostream>

namespace dnp3bridge::dnp3 {

// ---------------------------------------------------------------------------
// StreamToken
// ---------------------------------------------------------------------------

CommandDispatcher::StreamToken::StreamToken(CommandDispatcher* dispatcher)
    : dispatcher_{dispatcher}
{}

CommandDispatcher::StreamToken::~StreamToken() {
    if (dispatcher_) {
        std::lock_guard lock{dispatcher_->writer_mutex_};
        dispatcher_->active_writer_ = nullptr;
    }
}

CommandDispatcher::StreamToken::StreamToken(StreamToken&& other) noexcept
    : dispatcher_{other.dispatcher_}
{
    other.dispatcher_ = nullptr;
}

CommandDispatcher::StreamToken& CommandDispatcher::StreamToken::operator=(StreamToken&& other) noexcept {
    if (this != &other) {
        // Clean up current registration if any.
        if (dispatcher_) {
            std::lock_guard lock{dispatcher_->writer_mutex_};
            dispatcher_->active_writer_ = nullptr;
        }
        dispatcher_ = other.dispatcher_;
        other.dispatcher_ = nullptr;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// CommandDispatcher
// ---------------------------------------------------------------------------

CommandDispatcher::CommandDispatcher(std::chrono::milliseconds timeout)
    : timeout_{timeout}
{}

CommandDispatcher::StreamToken CommandDispatcher::registerWriter(
    ::grpc::ServerWriter<dnp3bridge::v1::CommandRequest>* writer)
{
    {
        std::lock_guard lock{writer_mutex_};
        active_writer_ = writer;
    }
    std::cerr << "[CommandDispatcher] Stream writer registered\n";
    return StreamToken{this};
}

opendnp3::CommandStatus CommandDispatcher::dispatch(dnp3bridge::v1::CommandRequest request) {
    const auto command_id = next_id_.fetch_add(1, std::memory_order_relaxed);
    request.set_command_id(command_id);

    std::future<opendnp3::CommandStatus> future;
    {
        std::lock_guard lock{pending_mutex_};
        auto& promise = pending_[command_id];
        future = promise.get_future();
    }

    {
        std::lock_guard lock{writer_mutex_};
        if (!active_writer_) {
            std::lock_guard plock{pending_mutex_};
            pending_.erase(command_id);
            return opendnp3::CommandStatus::NOT_SUPPORTED;
        }
        if (!active_writer_->Write(request)) {
            std::lock_guard plock{pending_mutex_};
            pending_.erase(command_id);
            return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
        }
    }

    if (future.wait_for(timeout_) == std::future_status::timeout) {
        std::lock_guard lock{pending_mutex_};
        pending_.erase(command_id);
        return opendnp3::CommandStatus::TIMEOUT;
    }

    return future.get();
}

bool CommandDispatcher::fulfill(uint64_t command_id, opendnp3::CommandStatus status) {
    std::lock_guard lock{pending_mutex_};
    auto it = pending_.find(command_id);
    if (it == pending_.end()) {
        return false;
    }
    it->second.set_value(status);
    pending_.erase(it);
    return true;
}

} // namespace dnp3bridge::dnp3
