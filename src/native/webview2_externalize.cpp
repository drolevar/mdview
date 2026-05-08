#include "native/webview2_externalize.hpp"

#include <shellapi.h>

namespace mdview::detail {

ShellOpenFn& shell_open_hook() noexcept {
    static ShellOpenFn fn = &::ShellExecuteW;
    return fn;
}

bool is_internal_uri(std::wstring_view uri) noexcept {
    return uri.starts_with(L"https://mdview-app.example/") ||
           uri.starts_with(L"https://mdview-doc.example/");
}

void externalize_uri(LPCWSTR uri) noexcept {
    if (uri == nullptr) return;
    shell_open_hook()(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}

}
