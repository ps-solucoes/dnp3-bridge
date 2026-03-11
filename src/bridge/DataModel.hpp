#pragma once

#include <cstdint>
#include <variant>

namespace dnp3bridge::bridge {

struct AnalogUpdate {
    std::uint16_t index;
    double        value;
};

struct BinaryUpdate {
    std::uint16_t index;
    bool          value;
};

struct CounterUpdate {
    std::uint16_t index;
    std::uint32_t value;
};

using PointUpdate = std::variant<AnalogUpdate, BinaryUpdate, CounterUpdate>;

} // namespace dnp3bridge::bridge
