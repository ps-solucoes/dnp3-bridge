#pragma once

#include <memory>
#include <mutex>

#include <opendnp3/master/ISOEHandler.h>

namespace dnp3sim {

class SoeHandler final : public opendnp3::ISOEHandler
{
public:
    static std::shared_ptr<opendnp3::ISOEHandler> Create();

    void BeginFragment(const opendnp3::ResponseInfo& info) override;
    void EndFragment(const opendnp3::ResponseInfo& info) override;

    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Binary>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::DoubleBitBinary>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Analog>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Counter>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::FrozenCounter>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryOutputStatus>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogOutputStatus>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::OctetString>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::TimeAndInterval>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryCommandEvent>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogCommandEvent>>& values) override;
    void Process(const opendnp3::HeaderInfo& info, const opendnp3::ICollection<opendnp3::DNPTime>& values) override;

private:
    std::mutex mutex_;
};

} // namespace dnp3sim
