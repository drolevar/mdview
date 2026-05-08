#pragma once

#include <string_view>

namespace mdview {

enum class Theme {
    System,   // sentinel: unspecified; renderer falls back to prefers-color-scheme
    Light,
    Dark,
};

constexpr std::wstring_view to_wire(Theme t) noexcept {
    switch (t) {
    case Theme::Light:  return L"light";
    case Theme::Dark:   return L"dark";
    case Theme::System: return L"system";
    }
    return L"system";
}

}
