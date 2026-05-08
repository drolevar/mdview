#include "session.hpp"

#include "pump.hpp"
#include "screenshot.hpp"

#include <chrono>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>

namespace mdview::integration {

DebugMonitor*       Session::global_monitor = nullptr;
bool                Session::hidden         = false;
std::filesystem::path Session::smoke_dir;

namespace {

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

Session::Session() : monitor_(global_monitor) {
    if (monitor_) monitor_->clear();
    create_parent_window_();
    load_dll_();
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
    close();
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

void Session::load_dll_() {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path dll = std::filesystem::path{exe_path}
                                   .parent_path() / L"mdview.wlx64";
    dll_ = ::LoadLibraryW(dll.c_str());
    if (!dll_) {
        // Fallback: search alongside other build outputs.
        dll = std::filesystem::path{exe_path}.parent_path()
                  .parent_path() / L"src" / L"mdview.wlx64";
        dll_ = ::LoadLibraryW(dll.c_str());
    }
    if (!dll_) {
        throw std::runtime_error("LoadLibrary mdview.wlx64 failed");
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
        throw std::runtime_error("WLX exports missing in mdview.wlx64");
    }

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
    if (dll_) ::FreeLibrary(dll_);
    dll_ = nullptr;
}

std::optional<RenderedSummary>
Session::wait_for_summary(std::chrono::milliseconds timeout) {
    if (!monitor_) return std::nullopt;

    auto matches_any_summary = [](const std::wstring& line) {
        if (line.find(L"viewer: rendered id=") == std::wstring::npos)
            return false;
        return line.find(L"summary=") != std::wstring::npos
            || line.find(L"summary[") != std::wstring::npos;
    };

    bool got = pump_until(
        [&]() {
            for (auto& line : monitor_->snapshot()) {
                if (matches_any_summary(line)) return true;
            }
            return false;
        },
        timeout, monitor_->log_event());

    if (!got) return std::nullopt;

    captured_log_ = monitor_->snapshot();

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
    if (monitor_) monitor_->clear();
    captured_log_.clear();
}

}
