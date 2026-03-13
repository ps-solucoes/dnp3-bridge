#include "SoeHandler.hpp"
#include "PointNames.hpp"

#include <iomanip>
#include <iostream>

namespace dnp3sim {

std::shared_ptr<opendnp3::ISOEHandler> SoeHandler::Create()
{
    return std::make_shared<SoeHandler>();
}

void SoeHandler::BeginFragment(const opendnp3::ResponseInfo& /*info*/)
{
    mutex_.lock();
    std::cout << "--- Begin Response ---\n";
}

void SoeHandler::EndFragment(const opendnp3::ResponseInfo& /*info*/)
{
    std::cout << "--- End Response ---\n" << std::flush;
    mutex_.unlock();
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Binary>>& values)
{
    values.ForeachItem([](const opendnp3::Indexed<opendnp3::Binary>& pair) {
        std::cout << "  [BI " << std::setw(2) << pair.index << "] "
                  << std::setw(30) << std::left
                  << pointName(kBinaryInputNames, pair.index)
                  << " : " << pair.value.value
                  << "  (flags=0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(pair.value.flags.value)
                  << std::dec << std::setfill(' ') << ")\n";
    });
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Analog>>& values)
{
    values.ForeachItem([](const opendnp3::Indexed<opendnp3::Analog>& pair) {
        std::cout << "  [AI " << std::setw(2) << pair.index << "] "
                  << std::setw(30) << std::left
                  << pointName(kAnalogInputNames, pair.index)
                  << " : " << pair.value.value
                  << "  (flags=0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(pair.value.flags.value)
                  << std::dec << std::setfill(' ') << ")\n";
    });
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryOutputStatus>>& values)
{
    values.ForeachItem([](const opendnp3::Indexed<opendnp3::BinaryOutputStatus>& pair) {
        std::cout << "  [BO " << std::setw(2) << pair.index << "] "
                  << std::setw(30) << std::left
                  << pointName(kBinaryOutputNames, pair.index)
                  << " : " << pair.value.value
                  << "  (flags=0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(pair.value.flags.value)
                  << std::dec << std::setfill(' ') << ")\n";
    });
}

void SoeHandler::Process(const opendnp3::HeaderInfo& /*info*/,
                         const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogOutputStatus>>& values)
{
    values.ForeachItem([](const opendnp3::Indexed<opendnp3::AnalogOutputStatus>& pair) {
        std::cout << "  [AO " << std::setw(2) << pair.index << "] "
                  << std::setw(30) << std::left
                  << pointName(kAnalogOutputNames, pair.index)
                  << " : " << pair.value.value
                  << "  (flags=0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(pair.value.flags.value)
                  << std::dec << std::setfill(' ') << ")\n";
    });
}

// No-op overloads for types we don't care about.
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::DoubleBitBinary>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Counter>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::FrozenCounter>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::OctetString>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::TimeAndInterval>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryCommandEvent>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogCommandEvent>>&) {}
void SoeHandler::Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::DNPTime>&) {}

} // namespace dnp3sim
