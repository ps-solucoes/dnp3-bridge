#include <doctest/doctest.h>

#include "dnp3/CommandDispatcher.hpp"

#include <chrono>
#include <thread>

using namespace dnp3bridge::dnp3;
using namespace std::chrono_literals;

TEST_CASE("CommandDispatcher") {
    CommandDispatcher dispatcher{50ms};

    SUBCASE("dispatch with no writer returns NOT_SUPPORTED") {
        dnp3bridge::v1::CommandRequest req;
        req.set_point_index(0);
        auto status = dispatcher.dispatch(std::move(req));
        CHECK(status == opendnp3::CommandStatus::NOT_SUPPORTED);
    }

    SUBCASE("fulfill with unknown command_id returns false") {
        bool ok = dispatcher.fulfill(999, opendnp3::CommandStatus::SUCCESS);
        CHECK_FALSE(ok);
    }

    SUBCASE("fulfill with command_id 0 returns false") {
        bool ok = dispatcher.fulfill(0, opendnp3::CommandStatus::SUCCESS);
        CHECK_FALSE(ok);
    }

    SUBCASE("StreamToken RAII unregisters writer on destruction") {
        // Register a (fake) writer pointer -- we never actually call Write(),
        // we only need dispatch() to get past the nullptr check.
        // After the token is destroyed, dispatch should return NOT_SUPPORTED.
        {
            // Use a non-null sentinel; dispatch will try to call Write() on it
            // so we cannot safely test that path. Instead, verify that after
            // the token goes out of scope, the writer is cleared.
            auto token = dispatcher.registerWriter(
                reinterpret_cast<grpc::ServerWriter<dnp3bridge::v1::CommandRequest>*>(0x1));
            // token alive -- writer is registered
        }
        // token destroyed -- writer should be unregistered
        dnp3bridge::v1::CommandRequest req;
        auto status = dispatcher.dispatch(std::move(req));
        CHECK(status == opendnp3::CommandStatus::NOT_SUPPORTED);
    }

    SUBCASE("StreamToken move leaves source disarmed") {
        auto token1 = dispatcher.registerWriter(
            reinterpret_cast<grpc::ServerWriter<dnp3bridge::v1::CommandRequest>*>(0x1));

        // Move into token2 -- token1 should be disarmed (no double-unregister).
        auto token2 = std::move(token1);

        // Destroying token2 unregisters the writer.
    }

    SUBCASE("multiple sequential dispatches without writer all return NOT_SUPPORTED") {
        for (int i = 0; i < 5; ++i) {
            dnp3bridge::v1::CommandRequest req;
            req.set_point_index(static_cast<uint32_t>(i));
            auto status = dispatcher.dispatch(std::move(req));
            CHECK(status == opendnp3::CommandStatus::NOT_SUPPORTED);
        }
    }

    // NOTE: Testing the happy path (dispatch + fulfill) and timeout requires
    // a real grpc::ServerWriter that can handle Write() calls. Since
    // grpc::ServerWriter requires gRPC internal state (grpc::internal::Call)
    // and cannot be trivially mocked, those paths are not covered here.
    // Integration tests or a testable writer abstraction would be needed.
}
