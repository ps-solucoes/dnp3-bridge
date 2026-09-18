#include "bridge/Bridge.hpp"
#include "dnp3/OutstationManager.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace dnp3bridge::bridge {

Bridge::Bridge(dnp3::OutstationManager& outstation)
    : outstation_{outstation}
{}

Bridge::~Bridge() {
    stop();
}

void Bridge::applyUpdate(PointUpdate update) {
    {
        std::lock_guard lock{mutex_};
        queue_.push(std::move(update));
    }
    cv_.notify_one();
}

void Bridge::start() {
    {
        std::lock_guard lock{mutex_};
        if (running_) return;
        running_ = true;
    }
    spdlog::info("Bridge flush thread starting");
    flush_thread_ = std::jthread{[this] { flushLoop(); }};
}

void Bridge::stop() {
    {
        std::lock_guard lock{mutex_};
        if (!running_) return;
        running_ = false;
    }
    spdlog::info("Bridge flush thread stopping");
    cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

void Bridge::flushLoop() {
    while (true) {
        std::queue<PointUpdate> batch;

        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty()) {
                return;
            }

            std::swap(batch, queue_);
        }

        spdlog::trace("Bridge flushing {} updates", batch.size());

        // Drain the batch outside the lock.
        while (!batch.empty()) {
            auto& update = batch.front();

            std::visit([this](auto&& u) {
                using T = std::decay_t<decltype(u)>;
                if constexpr (std::is_same_v<T, AnalogUpdate>) {
                    outstation_.updateAnalog(u.index, u.value, u.quality);
                } else if constexpr (std::is_same_v<T, BinaryUpdate>) {
                    outstation_.updateBinary(u.index, u.value, u.quality);
                }
            }, update);

            batch.pop();
        }
    }
}

} // namespace dnp3bridge::bridge
