#include "dnp3/PointMap.hpp"

namespace dnp3bridge::dnp3 {

void PointMap::add(std::string name, PointAddress address) {
    map_.emplace(std::move(name), address);
}

auto PointMap::resolve(std::string_view name) const
    -> std::optional<PointAddress>
{
    auto it = map_.find(std::string{name});
    if (it != map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace dnp3bridge::dnp3
