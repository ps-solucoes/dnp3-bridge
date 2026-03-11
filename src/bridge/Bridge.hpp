#pragma once

#include "bridge/DataModel.hpp"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace dnp3bridge::dnp3 {
class OutstationManager; // forward declaration
}

namespace dnp3bridge::bridge {

/// Thread-safe bridge that queues point updates and flushes them to the
/// OutstationManager on a background thread.
class Bridge {
public:
    explicit Bridge(dnp3::OutstationManager& outstation);
    ~Bridge();

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    /// Enqueue a point update (thread-safe).
    void applyUpdate(PointUpdate update);

    /// Start the internal flush thread.
    void start();

    /// Signal the flush thread to stop and join it.
    void stop();

private:
    void flushLoop();

    dnp3::OutstationManager&    outstation_;
    std::queue<PointUpdate>     queue_;
    std::mutex                  mutex_;
    std::condition_variable     cv_;
    std::jthread                flush_thread_;
    bool                        running_{false};
};

} // namespace dnp3bridge::bridge
