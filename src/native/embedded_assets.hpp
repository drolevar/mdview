#pragma once

#include <span>
#include <string_view>

namespace mdview::assets {

// One embedded viewer asset. The viewer/dist + viewer/* tree is laid
// out at build time into a single root by the viewer_prestage CMake
// step; GenerateViewerResources.cmake walks it, sorts by URL path,
// and emits a constexpr array of these into viewer_assets.gen.cpp.
//
// path:         URL path with a leading '/', forward slashes only.
//               Lookup is case-sensitive (the generator emits the
//               on-disk casing, which on our build chain is always
//               lowercase except KaTeX font filenames).
// resource_id:  Win32 RCDATA resource ID (>= 2000) under the WLX
//               module. Use FindResourceExW(..., RT_RCDATA, ...).
// content_type: Resolved at gen-time from the file extension; emitted
//               as a wstring literal so this struct stays trivial.
struct EmbeddedAsset {
    std::wstring_view path;
    int               resource_id;
    std::wstring_view content_type;
};

// Defined in the generated viewer_assets.gen.cpp.
std::span<const EmbeddedAsset> table() noexcept;

// Binary-search the sorted table. Returns nullptr if path not found.
// Case-sensitive comparison.
const EmbeddedAsset* find_asset(std::wstring_view path) noexcept;

}  // namespace mdview::assets
