#include <doctest/doctest.h>

#include "bridge/Bridge.hpp"
#include "bridge/DataModel.hpp"
#include "config/AppConfig.hpp"
#include "dnp3/CommandDispatcher.hpp"
#include "dnp3/OutstationManager.hpp"

#include <chrono>
#include <thread>

using namespace dnp3bridge::bridge;
using namespace dnp3bridge::config;
using namespace dnp3bridge::dnp3;
using namespace std::chrono_literals;

TEST_CASE("Bridge lifecycle") {
    // Construct an OutstationManager without calling start(), so no network
    // ports are opened. The update methods early-return when outstation_ is
    // null, which is exactly what we want for unit testing.
    AppConfig cfg{};
    CommandDispatcher dispatcher{50ms};
    OutstationManager outstation{cfg, dispatcher};

    Bridge bridge{outstation};

    SUBCASE("start and stop without updates does not crash") {
        bridge.start();
        std::this_thread::sleep_for(10ms);
        bridge.stop();
    }

    SUBCASE("stop without start is safe") {
        bridge.stop();
    }

    SUBCASE("double start is safe") {
        bridge.start();
        bridge.start(); // second start should be a no-op
        bridge.stop();
    }

    SUBCASE("double stop is safe") {
        bridge.start();
        bridge.stop();
        bridge.stop();
    }

    SUBCASE("applyUpdate enqueues and flush loop drains") {
        bridge.start();

        bridge.applyUpdate(AnalogUpdate{.index = 0, .value = 3.14});
        bridge.applyUpdate(BinaryUpdate{.index = 1, .value = true});

        // Give the flush thread time to drain the queue.
        std::this_thread::sleep_for(50ms);

        bridge.stop();
        // No crash means the queue was drained and updates were forwarded
        // to OutstationManager (which early-returns since it was not started).
    }

    SUBCASE("applyUpdate before start queues items for later") {
        bridge.applyUpdate(AnalogUpdate{.index = 0, .value = 1.0});
        bridge.applyUpdate(AnalogUpdate{.index = 1, .value = 2.0});

        bridge.start();
        std::this_thread::sleep_for(50ms);
        bridge.stop();
    }

    SUBCASE("many rapid updates do not crash") {
        bridge.start();

        for (int i = 0; i < 100; ++i) {
            bridge.applyUpdate(AnalogUpdate{
                .index = static_cast<std::uint16_t>(i % 10),
                .value = static_cast<double>(i),
            });
        }

        std::this_thread::sleep_for(100ms);
        bridge.stop();
    }

    SUBCASE("destructor stops the flush thread") {
        {
            Bridge scoped_bridge{outstation};
            scoped_bridge.start();
            scoped_bridge.applyUpdate(AnalogUpdate{.index = 0, .value = 1.0});
            // destructor calls stop()
        }
        // No hang or crash means the thread was properly joined.
    }
}
