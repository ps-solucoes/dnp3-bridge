#include "dnp3/ForwardingCommandHandler.hpp"

#include <opendnp3/gen/OperationType.h>

#include <iostream>

namespace dnp3bridge::dnp3 {

namespace {

dnp3bridge::v1::CrobOperationType toCrobOperation(opendnp3::OperationType op) {
    switch (op) {
        case opendnp3::OperationType::NUL:       return dnp3bridge::v1::CROB_OPERATION_NUL;
        case opendnp3::OperationType::PULSE_ON:  return dnp3bridge::v1::CROB_OPERATION_PULSE_ON;
        case opendnp3::OperationType::PULSE_OFF: return dnp3bridge::v1::CROB_OPERATION_PULSE_OFF;
        case opendnp3::OperationType::LATCH_ON:  return dnp3bridge::v1::CROB_OPERATION_LATCH_ON;
        case opendnp3::OperationType::LATCH_OFF: return dnp3bridge::v1::CROB_OPERATION_LATCH_OFF;
        default:                                  return dnp3bridge::v1::CROB_OPERATION_NUL;
    }
}

} // anonymous namespace

ForwardingCommandHandler::ForwardingCommandHandler(CommandDispatcher& dispatcher)
    : dispatcher_{dispatcher}
{}

std::shared_ptr<opendnp3::ICommandHandler> ForwardingCommandHandler::Create(CommandDispatcher& dispatcher) {
    return std::make_shared<ForwardingCommandHandler>(dispatcher);
}

// ---------------------------------------------------------------------------
// CROB
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::Select(
    const opendnp3::ControlRelayOutputBlock& /*command*/, uint16_t /*index*/)
{
    return opendnp3::CommandStatus::SUCCESS;
}

opendnp3::CommandStatus ForwardingCommandHandler::Operate(
    const opendnp3::ControlRelayOutputBlock& command, uint16_t index,
    opendnp3::IUpdateHandler& /*handler*/, opendnp3::OperateType /*opType*/)
{
    try {
        dnp3bridge::v1::CommandRequest request;
        request.set_point_index(index);
        request.set_command_type(dnp3bridge::v1::COMMAND_TYPE_CROB);

        auto* crob = request.mutable_crob();
        crob->set_operation(toCrobOperation(command.opType));
        crob->set_count(command.count);
        crob->set_on_time_ms(command.onTimeMS);
        crob->set_off_time_ms(command.offTimeMS);

        return dispatcher_.dispatch(std::move(request));
    } catch (const std::exception& e) {
        std::cerr << "[ForwardingCommandHandler] Operate(CROB) failed: " << e.what() << "\n";
        return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
    } catch (...) {
        std::cerr << "[ForwardingCommandHandler] Operate(CROB) failed with unknown exception\n";
        return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
    }
}

// ---------------------------------------------------------------------------
// AnalogOutputInt16
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::Select(
    const opendnp3::AnalogOutputInt16& /*command*/, uint16_t /*index*/)
{
    return opendnp3::CommandStatus::SUCCESS;
}

opendnp3::CommandStatus ForwardingCommandHandler::Operate(
    const opendnp3::AnalogOutputInt16& command, uint16_t index,
    opendnp3::IUpdateHandler& /*handler*/, opendnp3::OperateType /*opType*/)
{
    return dispatchAnalog(index, static_cast<double>(command.value),
                          dnp3bridge::v1::COMMAND_TYPE_ANALOG_INT16);
}

// ---------------------------------------------------------------------------
// AnalogOutputInt32
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::Select(
    const opendnp3::AnalogOutputInt32& /*command*/, uint16_t /*index*/)
{
    return opendnp3::CommandStatus::SUCCESS;
}

opendnp3::CommandStatus ForwardingCommandHandler::Operate(
    const opendnp3::AnalogOutputInt32& command, uint16_t index,
    opendnp3::IUpdateHandler& /*handler*/, opendnp3::OperateType /*opType*/)
{
    return dispatchAnalog(index, static_cast<double>(command.value),
                          dnp3bridge::v1::COMMAND_TYPE_ANALOG_INT32);
}

// ---------------------------------------------------------------------------
// AnalogOutputFloat32
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::Select(
    const opendnp3::AnalogOutputFloat32& /*command*/, uint16_t /*index*/)
{
    return opendnp3::CommandStatus::SUCCESS;
}

opendnp3::CommandStatus ForwardingCommandHandler::Operate(
    const opendnp3::AnalogOutputFloat32& command, uint16_t index,
    opendnp3::IUpdateHandler& /*handler*/, opendnp3::OperateType /*opType*/)
{
    return dispatchAnalog(index, static_cast<double>(command.value),
                          dnp3bridge::v1::COMMAND_TYPE_ANALOG_FLOAT32);
}

// ---------------------------------------------------------------------------
// AnalogOutputDouble64
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::Select(
    const opendnp3::AnalogOutputDouble64& /*command*/, uint16_t /*index*/)
{
    return opendnp3::CommandStatus::SUCCESS;
}

opendnp3::CommandStatus ForwardingCommandHandler::Operate(
    const opendnp3::AnalogOutputDouble64& command, uint16_t index,
    opendnp3::IUpdateHandler& /*handler*/, opendnp3::OperateType /*opType*/)
{
    return dispatchAnalog(index, command.value,
                          dnp3bridge::v1::COMMAND_TYPE_ANALOG_DOUBLE64);
}

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

opendnp3::CommandStatus ForwardingCommandHandler::dispatchAnalog(
    uint16_t index, double value, dnp3bridge::v1::CommandType type)
{
    try {
        dnp3bridge::v1::CommandRequest request;
        request.set_point_index(index);
        request.set_command_type(type);
        request.set_analog_value(value);

        return dispatcher_.dispatch(std::move(request));
    } catch (const std::exception& e) {
        std::cerr << "[ForwardingCommandHandler] Operate(Analog) failed: " << e.what() << "\n";
        return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
    } catch (...) {
        std::cerr << "[ForwardingCommandHandler] Operate(Analog) failed with unknown exception\n";
        return opendnp3::CommandStatus::DOWNSTREAM_FAIL;
    }
}

} // namespace dnp3bridge::dnp3
