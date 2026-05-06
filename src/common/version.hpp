#pragma once

#include <compare>
#include <cstdint>

namespace mdview {

struct PluginInterfaceVersion {
    std::uint32_t hi;
    std::uint32_t low;

    constexpr auto operator<=>(const PluginInterfaceVersion&) const = default;

    constexpr bool at_least(PluginInterfaceVersion required) const {
        return *this >= required;
    }
};

}
