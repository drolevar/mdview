#include "native/webview2_environment.hpp"

#include "native/debug_log.hpp"
#include "native/runtime_version.hpp"
#include "native/viewer_paths.hpp"

#include <wrl.h>
#include <wil/resource.h>

#include <filesystem>

namespace mdview {

WebView2Environment& WebView2Environment::instance() {
    // Intentionally leaked: COM cannot be safely Released in DllMain.
    // Process-attached singleton; reclaimed by the OS at process exit.
    static WebView2Environment* p = new WebView2Environment();
    return *p;
}

void WebView2Environment::ensure_initialized(EnvCallback on_ready) {
    switch (state_) {
    case State::Ready:
        if (on_ready) on_ready(S_OK, env_.get());
        return;
    case State::Failed:
        if (on_ready) on_ready(cached_hr_, nullptr);
        return;
    case State::Initializing:
        pending_.push_back(std::move(on_ready));
        return;
    case State::NotStarted:
        pending_.push_back(std::move(on_ready));
        start_initialization_();
        return;
    }
}

wil::com_ptr<ICoreWebView2Environment>
WebView2Environment::get() const noexcept {
    return env_;
}

void WebView2Environment::start_initialization_() {
    state_ = State::Initializing;

    auto udf = resolve_webview2_udf();
    if (udf.empty()) {
        debug_log::log(L"env init aborted: cannot resolve user data folder");
        deliver_(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND), nullptr);
        return;
    }

    try {
        std::filesystem::create_directories(udf);
    } catch (...) {
        debug_log::log(L"env init: create_directories({}) failed", udf.wstring());
        deliver_(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND), nullptr);
        return;
    }

    debug_log::log(L"env init starting; udf={}", udf.wstring());

    auto handler =
        Microsoft::WRL::Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT hr, ICoreWebView2Environment* env) noexcept {
                this->deliver_(hr, env);
                return S_OK;
            });

    HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(
        /*browserExecutableFolder*/ nullptr,
        udf.c_str(),
        /*environmentOptions*/ nullptr,
        handler.Get());

    if (FAILED(hr)) {
        deliver_(hr, nullptr);
    }
}

// deliver_ fires queued callbacks synchronously, in order, on the UI
// thread. Re-entry contract: a callback that itself calls
// ensure_initialized() will, in the Ready/Failed states, receive a
// synchronous nested invocation; in the Initializing state, that new
// request lands in pending_ (which is empty here because we drained
// into a local) and will be delivered on the next state transition.
// Either way, no partially-iterated queue is exposed.
void WebView2Environment::deliver_(HRESULT hr,
                                   ICoreWebView2Environment* env) {
    if (SUCCEEDED(hr) && env != nullptr) {
        env_ = env;
        state_ = State::Ready;
        debug_log::log(L"env ready");

        // One-shot heads-up when the runtime is the frozen,
        // unpatched pre-110 line (what Windows 7/8.1 cap at).
        // Log-only by design (no UI, no ini). deliver_ reaches
        // Ready once per process (leaked singleton), so this
        // fires at most once.
        wil::unique_cotaskmem_string ver;
        if (SUCCEEDED(env_->get_BrowserVersionString(&ver)) &&
            ver && is_unpatched_legacy_runtime(ver.get())) {
            debug_log::log(
                L"runtime: legacy WebView2 {} (<110), unpatched "
                L"since 2023-10; expected on Windows 7/8.1 -- "
                L"rendering works, engine is not security-serviced",
                ver.get());
        }
    } else {
        cached_hr_ = SUCCEEDED(hr) ? E_FAIL : hr;
        state_ = State::Failed;
        debug_log::log(L"env failed: hr=0x{:08X}",
                       static_cast<uint32_t>(cached_hr_));
    }

    auto callbacks = std::move(pending_);
    pending_.clear();
    HRESULT delivery_hr = (state_ == State::Ready) ? S_OK : cached_hr_;
    for (auto& cb : callbacks) {
        if (cb) cb(delivery_hr, env_.get());
    }
}

}
