#include <doctest/doctest.h>

#include "dnp3/ForwardingCommandHandler.hpp"

using namespace dnp3bridge::dnp3;

TEST_CASE("ForwardingCommandHandler Select behavior") {
    CommandDispatcher dispatcher{std::chrono::milliseconds{50}};

    SUBCASE("DirectOperate mode: Select returns NOT_SUPPORTED") {
        auto handler = ForwardingCommandHandler::Create(dispatcher, CommandMode::DirectOperate);

        CHECK(handler->Select(opendnp3::ControlRelayOutputBlock(opendnp3::OperationType::LATCH_ON), 0)
              == opendnp3::CommandStatus::NOT_SUPPORTED);
        CHECK(handler->Select(opendnp3::AnalogOutputInt16(100), 0)
              == opendnp3::CommandStatus::NOT_SUPPORTED);
        CHECK(handler->Select(opendnp3::AnalogOutputInt32(100), 0)
              == opendnp3::CommandStatus::NOT_SUPPORTED);
        CHECK(handler->Select(opendnp3::AnalogOutputFloat32(1.0f), 0)
              == opendnp3::CommandStatus::NOT_SUPPORTED);
        CHECK(handler->Select(opendnp3::AnalogOutputDouble64(1.0), 0)
              == opendnp3::CommandStatus::NOT_SUPPORTED);
    }

    SUBCASE("SelectBeforeOperate mode: Select returns SUCCESS") {
        auto handler = ForwardingCommandHandler::Create(dispatcher, CommandMode::SelectBeforeOperate);

        CHECK(handler->Select(opendnp3::ControlRelayOutputBlock(opendnp3::OperationType::LATCH_ON), 0)
              == opendnp3::CommandStatus::SUCCESS);
        CHECK(handler->Select(opendnp3::AnalogOutputInt16(100), 0)
              == opendnp3::CommandStatus::SUCCESS);
        CHECK(handler->Select(opendnp3::AnalogOutputInt32(100), 0)
              == opendnp3::CommandStatus::SUCCESS);
        CHECK(handler->Select(opendnp3::AnalogOutputFloat32(1.0f), 0)
              == opendnp3::CommandStatus::SUCCESS);
        CHECK(handler->Select(opendnp3::AnalogOutputDouble64(1.0), 0)
              == opendnp3::CommandStatus::SUCCESS);
    }
}
