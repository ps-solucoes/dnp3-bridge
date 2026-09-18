#pragma once

#include <cstdint>
#include <variant>

namespace dnp3bridge::bridge {

/// Point quality as reported by Python, mirroring `v1::PointQuality`.
/// Mapped to DNP3 flags by the dnp3 layer.
enum class Quality : std::uint8_t {
    Good,
    Uncertain,
    Bad,
    Restart,
};

struct AnalogUpdate {
    std::uint16_t index;
    double        value;
    Quality       quality = Quality::Good;
};

struct BinaryUpdate {
    std::uint16_t index;
    bool          value;
    Quality       quality = Quality::Good;
};

using PointUpdate = std::variant<AnalogUpdate, BinaryUpdate>;

} // namespace dnp3bridge::bridge
