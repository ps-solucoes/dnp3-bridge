#pragma once

#include "dnp3/CommandDispatcher.hpp"

#include <opendnp3/outstation/ICommandHandler.h>
#include <opendnp3/app/ControlRelayOutputBlock.h>
#include <opendnp3/app/AnalogOutput.h>
#include <opendnp3/gen/CommandStatus.h>
#include <opendnp3/gen/OperateType.h>

#include <memory>

namespace dnp3bridge::dnp3 {

class ForwardingCommandHandler : public opendnp3::ICommandHandler {
public:
    explicit ForwardingCommandHandler(CommandDispatcher& dispatcher);

    static std::shared_ptr<ICommandHandler> Create(CommandDispatcher& dispatcher);

    void Begin() override {}
    void End() override {}

    // CROB
    opendnp3::CommandStatus Select(const opendnp3::ControlRelayOutputBlock& command, uint16_t index) override;
    opendnp3::CommandStatus Operate(const opendnp3::ControlRelayOutputBlock& command, uint16_t index,
                                     opendnp3::IUpdateHandler& handler, opendnp3::OperateType opType) override;

    // AnalogOutputInt16
    opendnp3::CommandStatus Select(const opendnp3::AnalogOutputInt16& command, uint16_t index) override;
    opendnp3::CommandStatus Operate(const opendnp3::AnalogOutputInt16& command, uint16_t index,
                                     opendnp3::IUpdateHandler& handler, opendnp3::OperateType opType) override;

    // AnalogOutputInt32
    opendnp3::CommandStatus Select(const opendnp3::AnalogOutputInt32& command, uint16_t index) override;
    opendnp3::CommandStatus Operate(const opendnp3::AnalogOutputInt32& command, uint16_t index,
                                     opendnp3::IUpdateHandler& handler, opendnp3::OperateType opType) override;

    // AnalogOutputFloat32
    opendnp3::CommandStatus Select(const opendnp3::AnalogOutputFloat32& command, uint16_t index) override;
    opendnp3::CommandStatus Operate(const opendnp3::AnalogOutputFloat32& command, uint16_t index,
                                     opendnp3::IUpdateHandler& handler, opendnp3::OperateType opType) override;

    // AnalogOutputDouble64
    opendnp3::CommandStatus Select(const opendnp3::AnalogOutputDouble64& command, uint16_t index) override;
    opendnp3::CommandStatus Operate(const opendnp3::AnalogOutputDouble64& command, uint16_t index,
                                     opendnp3::IUpdateHandler& handler, opendnp3::OperateType opType) override;

private:
    opendnp3::CommandStatus dispatchAnalog(uint16_t index, double value,
                                            dnp3bridge::v1::CommandType type);

    CommandDispatcher& dispatcher_;
};

} // namespace dnp3bridge::dnp3
