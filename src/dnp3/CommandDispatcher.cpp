#include "dnp3/CommandDispatcher.hpp"

#include <spdlog/spdlog.h>

namespace dnp3bridge::dnp3 {

// ---------------------------------------------------------------------------
// StreamToken
// ---------------------------------------------------------------------------

CommandDispatcher::StreamToken::StreamToken(CommandDispatcher* dispatcher)
    : dispatcher_{dispatcher}
{}

CommandDispatcher::StreamToken::~StreamToken() {
    if (dispatcher_) {
        spdlog::info("Command stream writer unregistered");
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
    spdlog::info("Command stream writer registered");
    return StreamToken{this};
}

opendnp3::CommandStatus CommandDispatcher::dispatch(dnp3bridge::v1::CommandRequest request) {
    const auto command_id = next_id_.fetch_add(1, std::memory_order_relaxed);
    request.set_command_id(command_id);
    spdlog::debug("Dispatching command id={} type={} index={}", command_id, static_cast<int>(request.command_type()), request.point_index());

    std::future<opendnp3::CommandStatus> future;
    {
        std::lock_guard lock{pending_mutex_};
        auto& promise = pending_[command_id];
        future = promise.get_future();
    }

    {
        std::lock_guard lock{writer_mutex_};
        if (!active_writer_) {
            spdlog::warn("Command id={} rejected: no active stream writer", command_id);
            std::lock_guard plock{pending_mutex_};
            pending_.erase(command_id);
            return opendnp3::CommandStatus::NOT_SUPPORTED;
        }
        try {
            if (!active_writer_->Write(request)) {
                spdlog::warn("Command id={} failed: stream write returned false", command_id);
                std::lock_guard plock{pending_mutex_};
                pending_.erase(command_id);
                return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
            }
        } catch (const std::exception& e) {
            spdlog::error("Command dispatch write failed: {}", e.what());
            std::lock_guard plock{pending_mutex_};
            pending_.erase(command_id);
            return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
        } catch (...) {
            spdlog::error("Command dispatch write failed with unknown exception");
            std::lock_guard plock{pending_mutex_};
            pending_.erase(command_id);
            return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
        }
    }

    if (future.wait_for(timeout_) == std::future_status::timeout) {
        spdlog::warn("Command id={} timed out after {}ms", command_id, timeout_.count());
        std::lock_guard lock{pending_mutex_};
        pending_.erase(command_id);
        return opendnp3::CommandStatus::TIMEOUT;
    }

    auto result = future.get();
    spdlog::debug("Command id={} fulfilled with status={}", command_id, static_cast<int>(result));
    return result;
}

bool CommandDispatcher::fulfill(uint64_t command_id, opendnp3::CommandStatus status) {
    spdlog::debug("Fulfilling command id={} with status={}", command_id, static_cast<int>(status));
    std::lock_guard lock{pending_mutex_};
    auto it = pending_.find(command_id);
    if (it == pending_.end()) {
        spdlog::warn("Fulfill failed: unknown command id={}", command_id);
        return false;
    }
    it->second.set_value(status);
    pending_.erase(it);
    return true;
}

} // namespace dnp3bridge::dnp3
