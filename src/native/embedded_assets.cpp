#include "native/embedded_assets.hpp"

#include <algorithm>

namespace mdview::assets {

const EmbeddedAsset* find_asset(std::wstring_view path) noexcept {
    const auto t = table();
    auto it = std::lower_bound(
        t.begin(), t.end(), path,
        [](const EmbeddedAsset& a, std::wstring_view p) {
            return a.path < p;
        });
    if (it != t.end() && it->path == path) {
        return &*it;
    }
    return nullptr;
}

}  // namespace mdview::assets
