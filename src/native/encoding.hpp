#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace mdview::encoding {

// Decode bytes to a UTF-16 wstring using BOM-led detection with
// UTF-8 strict / CP_ACP fallback. Never throws on data; throws only
// on OOM (allocation failures inside std::wstring or std::vector
// during conversion).
std::wstring decode(std::span<const std::byte> bytes);

}
