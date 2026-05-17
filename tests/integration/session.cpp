#include "session.hpp"

#include "pump.hpp"
#include "screenshot.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace mdview::integration {

Session*              Session::current_      = nullptr;
bool                  Session::hidden        = false;
std::filesystem::path Session::smoke_dir;

namespace {

// WLX filename for the bitness this test exe was built as. Mirrors
// cmake/TcmdPlugin.cmake: 64-bit -> mdview.wlx64, ARM64 ->
// mdview.wlxa64, 32-bit -> mdview.wlx. The integration exe and the
// WLX are produced by the same CMake preset, so the exe's own
// bitness always matches the WLX it must load - a compile-time
// selector is correct here.
#if defined(_M_ARM64) || defined(__aarch64__)
constexpr const wchar_t* kWlxName = L"mdview.wlxa64";
#elif defined(_WIN64)
constexpr const wchar_t* kWlxName = L"mdview.wlx64";
#else
constexpr const wchar_t* kWlxName = L"mdview.wlx";
#endif

LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return ::DefWindowProcW(h, m, w, l);
}

void register_parent_class_once() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = parent_proc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MDVIEW_TEST_PARENT";
    ::RegisterClassExW(&wc);
    done = true;
}

void apply_summary_env(bool want) {
    // Setting "0" rather than clearing keeps the read side simple
    // (always reads the var, single contains-'1' check). Always
    // overwrite so previous loads can't bleed into this one.
    ::SetEnvironmentVariableW(L"MDVIEW_REQUEST_SUMMARY",
                              want ? L"1" : L"0");
}

}

Session::Session() {
    if (current_ != nullptr) {
        // Catch2 runs cases sequentially; this should never fire. If
        // it does, a previous Session leaked or a test spawned two.
        throw std::runtime_error(
            "Session: a prior instance is still alive");
    }
    current_ = this;
    try {
        sink_event_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        // The WLX serves viewer assets from embedded RCDATA and must
        // never read viewer/ from disk. Remove any viewer/ left next
        // to the WLX (e.g. from an old staged install) before loading
        // the DLL so an accidental disk read fails loudly rather than
        // silently falling back to a stale copy.
        const auto wlx_path = resolve_wlx_path();
        if (!wlx_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(
                wlx_path.parent_path() / L"viewer", ec);
        }
        create_parent_window_();
        load_dll_();
    } catch (...) {
        // Constructor failed: tear down anything that did get set up
        // and clear `current_` so the next Session can be created.
        if (fn_set_log_sink_) fn_set_log_sink_(nullptr);
        if (plugin_hwnd_ && fn_close_) fn_close_(plugin_hwnd_);
        plugin_hwnd_ = nullptr;
        if (parent_hwnd_) ::DestroyWindow(parent_hwnd_);
        parent_hwnd_ = nullptr;
        if (dll_) ::FreeLibrary(dll_);
        dll_ = nullptr;
        if (sink_event_) ::CloseHandle(sink_event_);
        sink_event_ = nullptr;
        current_ = nullptr;
        throw;
    }
}

Session::~Session() {
    bool save = false;
    {
        wchar_t buf[8] = {};
        const DWORD n = ::GetEnvironmentVariableW(
            L"MDVIEW_SCREENSHOT_ON_FAILURE", buf,
            static_cast<DWORD>(sizeof(buf) / sizeof(wchar_t)));
        save = (n > 0 && buf[0] == L'1');
    }
    if (save && parent_hwnd_) {
        std::filesystem::path dir = std::filesystem::current_path()
                                       / L"screenshots";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto ts = std::chrono::system_clock::now()
                            .time_since_epoch().count();
        save_window_screenshot(parent_hwnd_,
            dir / (std::to_wstring(ts) + L".png"));
    }
    if (fn_set_log_sink_) fn_set_log_sink_(nullptr);
    close();
    if (sink_event_) {
        ::CloseHandle(sink_event_);
        sink_event_ = nullptr;
    }
    if (current_ == this) current_ = nullptr;
}

void Session::create_parent_window_() {
    register_parent_class_once();
    DWORD style = WS_OVERLAPPEDWINDOW;
    parent_hwnd_ = ::CreateWindowExW(
        0, L"MDVIEW_TEST_PARENT", L"mdview integration test",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!parent_hwnd_) {
        throw std::runtime_error("CreateWindowExW failed for parent");
    }
    if (!hidden) {
        ::ShowWindow(parent_hwnd_, SW_SHOW);
        ::UpdateWindow(parent_hwnd_);
    }
}

std::filesystem::path Session::resolve_wlx_path() {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path exe_dir =
        std::filesystem::path{exe_path}.parent_path();
    // Same dir as the test exe (DLL staged next to it), or
    // `<build>/src/` for the standard layout where exe lives at
    // `<build>/tests/integration/`.
    std::filesystem::path candidates[] = {
        exe_dir / kWlxName,
        exe_dir.parent_path().parent_path() / L"src" / kWlxName,
    };
    for (auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
    }
    return {};
}

void Session::load_dll_() {
    const auto dll_path = resolve_wlx_path();
    if (!dll_path.empty()) {
        dll_ = ::LoadLibraryW(dll_path.c_str());
    }
    if (!dll_) {
        std::string msg = "LoadLibrary ";
        msg += std::filesystem::path{kWlxName}.string();
        msg += " failed";
        throw std::runtime_error(msg);
    }

    fn_set_params_ = reinterpret_cast<Fn_ListSetDefaultParams>(
        ::GetProcAddress(dll_, "ListSetDefaultParams"));
    fn_load_       = reinterpret_cast<Fn_ListLoadW>(
        ::GetProcAddress(dll_, "ListLoadW"));
    fn_load_next_  = reinterpret_cast<Fn_ListLoadNextW>(
        ::GetProcAddress(dll_, "ListLoadNextW"));
    fn_send_cmd_   = reinterpret_cast<Fn_ListSendCommand>(
        ::GetProcAddress(dll_, "ListSendCommand"));
    fn_close_      = reinterpret_cast<Fn_ListCloseWindow>(
        ::GetProcAddress(dll_, "ListCloseWindow"));

    if (!fn_set_params_ || !fn_load_ || !fn_close_) {
        std::string msg = "WLX exports missing in ";
        msg += std::filesystem::path{kWlxName}.string();
        throw std::runtime_error(msg);
    }

    fn_set_log_sink_ = reinterpret_cast<Fn_MdviewTest_SetLogSink>(
        ::GetProcAddress(dll_, "MdviewTest_SetLogSink"));
    if (!fn_set_log_sink_) {
        throw std::runtime_error(
            "WLX missing MdviewTest_SetLogSink export - "
            "rebuild with current changes");
    }
    fn_set_log_sink_(&Session::on_log_line_);

    struct ListDefaultParamStruct {
        int   size;
        DWORD verLow;
        DWORD verHi;
        char  ini[260];
    } dps{};
    dps.size = sizeof(dps);
    dps.verHi = 2; dps.verLow = 12;
    fn_set_params_(&dps);
}

void Session::on_log_line_(const wchar_t* line, size_t len) noexcept {
    Session* s = current_;
    if (!s) return;
    {
        std::lock_guard<std::mutex> lk(s->sink_mu_);
        s->sink_lines_.emplace_back(line, len);
    }
    if (s->sink_event_) ::SetEvent(s->sink_event_);
}

bool Session::load(const std::wstring& fixture_name,
                   WithSummary summary) {
    apply_summary_env(summary.value);
    auto path = (smoke_dir / fixture_name).wstring();
    plugin_hwnd_ = fn_load_(parent_hwnd_, path.data(), 0);
    last_doc_id_ = 1;
    return plugin_hwnd_ != nullptr;
}

bool Session::load_next(const std::wstring& fixture_name,
                        WithSummary summary) {
    if (!fn_load_next_ || !plugin_hwnd_) return false;
    apply_summary_env(summary.value);
    auto path = (smoke_dir / fixture_name).wstring();
    int rc = fn_load_next_(parent_hwnd_, plugin_hwnd_, path.data(), 0);
    ++last_doc_id_;
    return rc == 0;
}

int Session::send_command(int command, int parameter) {
    if (!fn_send_cmd_ || !plugin_hwnd_) return -1;
    return fn_send_cmd_(plugin_hwnd_, command, parameter);
}

void Session::close() {
    if (plugin_hwnd_ && fn_close_) fn_close_(plugin_hwnd_);
    plugin_hwnd_ = nullptr;
    if (parent_hwnd_) ::DestroyWindow(parent_hwnd_);
    parent_hwnd_ = nullptr;
    // Intentionally do NOT FreeLibrary the WLX. The WLX registers
    // window classes against its HMODULE; on reload the OS keeps the
    // class but the DLL's static state forgets it, so a second
    // RegisterClassExW with the same name fails. Keeping the DLL
    // loaded for the test process matches TC's real lifecycle (TC
    // never unloads a WLX) and keeps subsequent Sessions usable.
    dll_ = nullptr;
}

std::optional<RenderedSummary>
Session::wait_for_summary(std::chrono::milliseconds timeout) {
    auto matches_any_summary = [](const std::wstring& line) {
        if (line.find(L"viewer: rendered id=") == std::wstring::npos)
            return false;
        return line.find(L"summary=") != std::wstring::npos
            || line.find(L"summary[") != std::wstring::npos;
    };

    auto local_snapshot = [this]() {
        std::lock_guard<std::mutex> lk(sink_mu_);
        return std::vector<std::wstring>(
            sink_lines_.begin(), sink_lines_.end());
    };

    bool got = pump_until(
        [&]() {
            for (auto& line : local_snapshot()) {
                if (matches_any_summary(line)) return true;
            }
            return false;
        },
        timeout, sink_event_);

    // Snapshot regardless of timeout outcome - tests that exercise the
    // env-failed / fast-failure paths intentionally wait for no
    // summary but still need captured_log() to see the lines that
    // *did* fire.
    captured_log_ = local_snapshot();

    if (!got) return std::nullopt;

    std::wregex single_re(L"viewer: rendered id=\\d+ summary=(\\{.*\\})");
    std::wregex chunk_re(
        L"viewer: rendered id=\\d+ summary\\[(\\d+)/(\\d+)\\]=(.*)");

    std::wstring payload;
    std::vector<std::wstring> chunks;
    int total = 0;
    for (auto& line : captured_log_) {
        std::wsmatch m;
        if (std::regex_search(line, m, single_re)) {
            payload = m[1].str();
            return parse_summary_json(payload);
        }
        if (std::regex_search(line, m, chunk_re)) {
            int idx  = std::stoi(m[1].str());
            int tot  = std::stoi(m[2].str());
            if (chunks.size() < static_cast<size_t>(tot)) {
                chunks.resize(static_cast<size_t>(tot));
            }
            chunks[static_cast<size_t>(idx) - 1] = m[3].str();
            total = tot;
        }
    }
    if (total > 0) {
        for (auto& c : chunks) payload += c;
        return parse_summary_json(payload);
    }
    return std::nullopt;
}

const std::vector<std::wstring>& Session::captured_log() const {
    return captured_log_;
}

void Session::reset_log() {
    {
        std::lock_guard<std::mutex> lk(sink_mu_);
        sink_lines_.clear();
    }
    captured_log_.clear();
}

bool Session::wait_for_log_substring(
    const std::wstring& substr,
    std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    const auto poll_interval = std::chrono::milliseconds{50};

    while (clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(sink_mu_);
            for (const auto& line : sink_lines_) {
                if (line.find(substr) != std::wstring::npos) {
                    return true;
                }
            }
            for (const auto& line : captured_log_) {
                if (line.find(substr) != std::wstring::npos) {
                    return true;
                }
            }
        }
        // Pump messages while waiting so WebView2 callbacks can
        // flow through and dispatch new log lines into the sink.
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(poll_interval);
    }
    return false;
}

}
