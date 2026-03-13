#include "SoeHandler.hpp"
#include "PointNames.hpp"

#include <format>

namespace dnp3sim {

SoeHandler::SoeHandler(DataModel& model, std::function<void()> notify)
    : model_(model), notify_(std::move(notify))
{}

std::shared_ptr<opendnp3::ISOEHandler> SoeHandler::Create(DataModel& model, std::function<void()> notify)
{
    return std::make_shared<SoeHandler>(model, std::move(notify));
}

void SoeHandler::BeginFragment(const opendnp3::ResponseInfo& /*info*/) {}
void SoeHandler::EndFragment(const opendnp3::ResponseInfo& /*info*/) {}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Binary>>& values)
{
    auto now = std::chrono::steady_clock::now();
    int count = 0;
    {
        std::lock_guard lock(model_.mutex);
        values.ForeachItem([&](const opendnp3::Indexed<opendnp3::Binary>& pair) {
            model_.binary_inputs[pair.index] = {
                pair.value.value ? 1.0 : 0.0,
                pair.value.flags.value,
                true,
                now
            };
            ++count;
        });
        model_.addLog(std::format("Received {} binary input(s)", count));
    }
    notify_();
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Analog>>& values)
{
    auto now = std::chrono::steady_clock::now();
    int count = 0;
    {
        std::lock_guard lock(model_.mutex);
        values.ForeachItem([&](const opendnp3::Indexed<opendnp3::Analog>& pair) {
            model_.analog_inputs[pair.index] = {
                pair.value.value,
                pair.value.flags.value,
                true,
                now
            };
            ++count;
        });
        model_.addLog(std::format("Received {} analog input(s)", count));
    }
    notify_();
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryOutputStatus>>& values)
{
    auto now = std::chrono::steady_clock::now();
    int count = 0;
    {
        std::lock_guard lock(model_.mutex);
        values.ForeachItem([&](const opendnp3::Indexed<opendnp3::BinaryOutputStatus>& pair) {
            model_.binary_outputs[pair.index] = {
                pair.value.value ? 1.0 : 0.0,
                pair.value.flags.value,
                true,
                now
            };
            ++count;
        });
        model_.addLog(std::format("Received {} binary output status(es)", count));
    }
    notify_();
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogOutputStatus>>& values)
{
    auto now = std::chrono::steady_clock::now();
    int count = 0;
    {
        std::lock_guard lock(model_.mutex);
        values.ForeachItem([&](const opendnp3::Indexed<opendnp3::AnalogOutputStatus>& pair) {
            model_.analog_outputs[pair.index] = {
                pair.value.value,
                pair.value.flags.value,
                true,
                now
            };
            ++count;
        });
        model_.addLog(std::format("Received {} analog output status(es)", count));
    }
    notify_();
}

// No-op overloads for types we don't use.
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::DoubleBitBinary>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Counter>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::FrozenCounter>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::OctetString>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::TimeAndInterval>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryCommandEvent>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogCommandEvent>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::DNPTime>&) {}

} // namespace dnp3sim
