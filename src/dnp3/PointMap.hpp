#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dnp3bridge::dnp3 {

enum class PointType : std::uint8_t {
    Analog,
    Binary,
    Counter,
};

struct PointAddress {
    PointType     type;
    std::uint16_t index;
};

/// Maps human-readable point names to (type, index) pairs.
/// Stub implementation -- will be populated from configuration later.
class PointMap {
public:
    /// Register a named point.
    void add(std::string name, PointAddress address);

    /// Resolve a name to its address, or std::nullopt if not found.
    [[nodiscard]] auto resolve(std::string_view name) const
        -> std::optional<PointAddress>;

private:
    std::unordered_map<std::string, PointAddress> map_;
};

} // namespace dnp3bridge::dnp3
