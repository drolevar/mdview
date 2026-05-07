#pragma once

#include <windows.h>
#include <objbase.h>

#include <WebView2.h>

#include <wil/com.h>

#include <functional>
#include <vector>

namespace mdview {

using EnvCallback =
    std::function<void(HRESULT, ICoreWebView2Environment*)>;

class WebView2Environment {
public:
    static WebView2Environment& instance();

    // Async. If env is ready, callback runs synchronously here.
    // If init is in flight, callback is queued.
    // If init has previously failed, callback fires immediately
    // with the cached HRESULT.
    void ensure_initialized(EnvCallback on_ready);

    wil::com_ptr<ICoreWebView2Environment> get() const noexcept;

    WebView2Environment(const WebView2Environment&)            = delete;
    WebView2Environment& operator=(const WebView2Environment&) = delete;

private:
    WebView2Environment()  = default;
    ~WebView2Environment() = default;     // never called in practice

    enum class State { NotStarted, Initializing, Ready, Failed };

    void start_initialization_();
    void deliver_(HRESULT hr, ICoreWebView2Environment* env);

    State                                  state_ = State::NotStarted;
    HRESULT                                cached_hr_ = S_OK;
    wil::com_ptr<ICoreWebView2Environment> env_;
    std::vector<EnvCallback>               pending_;
};

}
