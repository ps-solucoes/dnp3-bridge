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

using PointUpdate = std::variant<AnalogUpdate, BinaryUpdate>;

} // namespace dnp3bridge::bridge
