#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace dnp3sim {

struct PointValue {
    double value = 0.0;
    uint8_t flags = 0;
    bool received = false;
    std::chrono::steady_clock::time_point last_update;
};

struct LogEntry {
    std::string text;
    std::chrono::steady_clock::time_point timestamp;
};

struct DataModel {
    mutable std::mutex mutex;

    std::map<uint16_t, PointValue> binary_inputs;
    std::map<uint16_t, PointValue> analog_inputs;
    std::map<uint16_t, PointValue> binary_outputs;
    std::map<uint16_t, PointValue> analog_outputs;

    std::deque<LogEntry> event_log;
    static constexpr size_t kMaxLogEntries = 200;

    bool connected = false;

    std::string last_poll_type;
    std::string last_poll_time;

    /// Must be called with mutex held.
    void addLog(const std::string& text) {
        if (event_log.size() >= kMaxLogEntries) {
            event_log.pop_front();
        }
        event_log.push_back({text, std::chrono::steady_clock::now()});
    }
};

} // namespace dnp3sim
