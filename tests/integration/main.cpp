#include <catch2/catch_session.hpp>

#include "session.hpp"

#include <windows.h>
#include <combaseapi.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool flag_set(int argc, wchar_t** argv, std::wstring_view name) {
    for (int i = 1; i < argc; ++i) {
        if (name == argv[i]) return true;
    }
    return false;
}

bool env_set_truthy(const wchar_t* name) {
    wchar_t buf[8] = {};
    DWORD n = ::GetEnvironmentVariableW(
        name, buf,
        static_cast<DWORD>(sizeof(buf) / sizeof(wchar_t)));
    return n > 0 && buf[0] == L'1';
}

}

int wmain(int argc, wchar_t** argv) {
    using namespace mdview::integration;

    // Total Commander initializes COM (STA) before loading WLX
    // plugins; mirror that here so WebView2 environment creation
    // doesn't fail with CO_E_NOTINITIALIZED when the WLX is exercised
    // via Session.
    HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co_hr) && co_hr != RPC_E_CHANGED_MODE) {
        std::wcerr << L"CoInitializeEx failed: 0x" << std::hex << co_hr << L"\n";
        return 4;
    }
    bool co_owned = SUCCEEDED(co_hr);

    Session::hidden = flag_set(argc, argv, L"--hidden")
                   || env_set_truthy(L"MDVIEW_HARNESS_HIDDEN");

    // Resolve smoke dir from env (set by CMake's add_test ENVIRONMENT
    // property), or fall back to a path relative to the test exe.
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(
        L"MDVIEW_SMOKE_DIR", buf, MAX_PATH);
    if (n > 0) {
        Session::smoke_dir = std::filesystem::path{buf};
    } else {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        Session::smoke_dir = std::filesystem::path{exe}
                                .parent_path().parent_path()
                                .parent_path() / L"tests" / L"smoke";
    }

    // Hand off to Catch2. Strip our own flags from argv before
    // converting to UTF-8 (Catch2's session.run takes char**).
    std::vector<std::string> kept;
    for (int i = 0; i < argc; ++i) {
        std::wstring w(argv[i]);
        if (w == L"--hidden") continue;
        int sz = ::WideCharToMultiByte(
            CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (sz <= 0) continue;
        std::string s(static_cast<size_t>(sz) - 1, '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
        kept.emplace_back(std::move(s));
    }
    std::vector<char*> ansi_argv;
    ansi_argv.reserve(kept.size());
    for (auto& s : kept) ansi_argv.push_back(s.data());

    int rc = Catch::Session().run(
        static_cast<int>(ansi_argv.size()), ansi_argv.data());

    if (co_owned) ::CoUninitialize();
    return rc;
}
