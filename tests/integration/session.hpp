#pragma once

#include "native/debug_log.hpp"
#include "summary.hpp"

#include <windows.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mdview::integration {

struct WithSummary { bool value = true; };

class Session {
public:
    Session();
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    HWND parent_hwnd() const noexcept { return parent_hwnd_; }
    HWND plugin_hwnd() const noexcept { return plugin_hwnd_; }

    bool load(const std::wstring& fixture_name,
              WithSummary summary = WithSummary{true});

    bool load_next(const std::wstring& fixture_name,
                   WithSummary summary = WithSummary{true});

    int send_command(int command, int parameter);

    void close();

    std::optional<RenderedSummary>
    wait_for_summary(std::chrono::milliseconds timeout =
                         std::chrono::milliseconds{10000});

    const std::vector<std::wstring>& captured_log() const;

    void reset_log();

private:
    void create_parent_window_();
    void load_dll_();

    HMODULE      dll_         = nullptr;
    HWND         parent_hwnd_ = nullptr;
    HWND         plugin_hwnd_ = nullptr;
    int          last_doc_id_ = 0;
    std::vector<std::wstring> captured_log_;

    static Session*               current_;   // nullable; one Session at a time
    std::mutex                    sink_mu_;
    std::deque<std::wstring>      sink_lines_;
    HANDLE                        sink_event_ = nullptr;

    static void on_log_line_(const wchar_t* line, size_t len) noexcept;

    using Fn_ListSetDefaultParams = void  (__stdcall*)(void*);
    using Fn_ListLoadW            = HWND  (__stdcall*)(HWND, wchar_t*, int);
    using Fn_ListLoadNextW        = int   (__stdcall*)(HWND, HWND, wchar_t*, int);
    using Fn_ListSendCommand      = int   (__stdcall*)(HWND, int, int);
    using Fn_ListCloseWindow      = void  (__stdcall*)(HWND);
    using Fn_MdviewTest_SetLogSink = void (*)(mdview::debug_log::LogSink) noexcept;
    Fn_ListSetDefaultParams  fn_set_params_   = nullptr;
    Fn_ListLoadW             fn_load_         = nullptr;
    Fn_ListLoadNextW         fn_load_next_    = nullptr;
    Fn_ListSendCommand       fn_send_cmd_     = nullptr;
    Fn_ListCloseWindow       fn_close_        = nullptr;
    Fn_MdviewTest_SetLogSink fn_set_log_sink_ = nullptr;

public:
    static bool                hidden;
    static std::filesystem::path smoke_dir;
};

}
