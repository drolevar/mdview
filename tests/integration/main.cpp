#include <catch2/catch_session.hpp>

#include "debug_monitor.hpp"
#include "pump.hpp"
#include "session.hpp"

#include <windows.h>

#include <chrono>
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

    DebugMonitor monitor;
    if (auto* err = monitor.start()) {
        std::wcerr << L"DebugMonitor::start failed: " << err << L"\n";
        return 2;
    }
    Session::global_monitor = &monitor;

    // Case-marker probe: emit a known [mdview] line, expect to see it
    // within 1 s. If another DBWIN_BUFFER consumer (DebugView, dbgview
    // MCP, Sysinternals) is attached, we won't see it — bail with an
    // explanatory message.
    monitor.clear();
    ::OutputDebugStringW(L"[mdview] integration: case-marker probe\n");
    const bool got_probe = pump_until(
        [&]() {
            for (auto& l : monitor.snapshot()) {
                if (l.find(L"case-marker probe") != std::wstring::npos)
                    return true;
            }
            return false;
        },
        std::chrono::milliseconds{1000}, monitor.log_event());
    if (!got_probe) {
        std::wcerr <<
            L"DebugMonitor case-marker probe failed.\n"
            L"Another DBWIN_BUFFER consumer (DebugView, dbgview MCP, "
            L"Sysinternals) appears to be active.\n"
            L"Stop it and re-run.\n";
        monitor.stop();
        return 3;
    }
    monitor.clear();

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

    const int rc = Catch::Session().run(
        static_cast<int>(ansi_argv.size()), ansi_argv.data());

    Session::global_monitor = nullptr;
    monitor.stop();
    return rc;
}
