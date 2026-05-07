#include "native/viewer_host.hpp"

#include "native/debug_log.hpp"
#include "native/host_names.hpp"
#include "native/renderer_protocol.hpp"

#include <cassert>
#include <utility>

namespace mdview {

ViewerHost::ViewerHost(ViewerOptions options,
                       std::unique_ptr<IWebView2Host> host)
    : options_(options)
    , host_(std::move(host))
    , alive_token_(std::make_shared<bool>(true)) {
    assert(host_);
}

ViewerHost::~ViewerHost() {
    alive_token_.reset();
    if (host_) host_->close();
}

void ViewerHost::create(HWND parent_hwnd, LifecycleCallback on_event) {
    on_event_ = std::move(on_event);
    state_    = State::EnvPending;
    debug_log::log(L"viewer-host create: env-pending");

    std::weak_ptr<bool> wp = alive_token_;
    host_->create(parent_hwnd,
        [this, wp](HRESULT hr) {
            auto alive = wp.lock();
            if (!alive) return;
            on_host_created_(hr);
        });
}

void ViewerHost::resize(RECT bounds) {
    if (host_) host_->resize(bounds);
}

void ViewerHost::focus() {
    if (host_) host_->focus();
}

void ViewerHost::load_document(DocumentRequest request) {
    if (state_ == State::Failed
     || state_ == State::Crashed
     || state_ == State::Closed) {
        return;
    }

    // Assign the monotonic id and remap the doc-dir virtual host
    // before either dispatching now or queuing for replay on ready.
    // The remap result is reflected in base_uri so the renderer sees
    // the same baseline whether it ran now or later.
    request.doc_id = ++doc_id_;
    if (!request.doc_dir.empty()) {
        const HRESULT hr = host_->remap_doc_dir(request.doc_dir);
        if (FAILED(hr)) {
            debug_log::log(
                L"viewer-host: remap failed hr=0x{:08X}",
                static_cast<uint32_t>(hr));
            request.base_uri.clear();
        } else {
            request.base_uri = kDocBaseUri;
        }
    }

    if (state_ == State::RendererReady || state_ == State::Loaded) {
        LoadDocumentMessage msg;
        msg.id           = request.doc_id;
        msg.path         = request.file_path;
        msg.display_name = request.display_name;
        msg.base_uri     = request.base_uri;
        msg.options      = options_;
        host_->post_to_renderer(encode_load_document(msg));
        state_ = State::Loaded;
        return;
    }
    pending_load_ = std::move(request);   // latest-wins
}

void ViewerHost::close() {
    state_ = State::Closed;
    pending_load_.reset();
    if (host_) host_->close();
}

void ViewerHost::on_host_created_(HRESULT hr) {
    if (FAILED(hr)) {
        state_ = State::Failed;
        debug_log::log(L"viewer-host: init failed, hr=0x{:08X}",
                       static_cast<uint32_t>(hr));
        if (on_event_) {
            on_event_(LifecycleEvent{
                LifecycleEvent::Kind::InitFailed, hr, 0});
        }
        return;
    }
    state_ = State::Navigated;
    debug_log::log(L"viewer-host: navigated");
}

void ViewerHost::dispatch_renderer_message(std::wstring_view json) {
    auto msg = decode_renderer_message(json);
    if (!msg) {
        debug_log::log(L"viewer-host: dropped malformed message");
        return;
    }
    if (std::holds_alternative<ReadyMessage>(*msg)) {
        if (state_ != State::Navigated) {
            debug_log::log(L"viewer-host: ignored ready in state {}",
                           static_cast<int>(state_));
            return;
        }
        state_ = State::RendererReady;
        debug_log::log(L"viewer-host: renderer ready");

        // Snapshot pending_load_ BEFORE firing the event. The user
        // callback may run for a long time and may itself call
        // load_document, close(), etc.; reading pending_load_ after
        // the callback risks observing partially-mutated state. With
        // the snapshot pattern, the user's re-armed pending_load_
        // (if any) takes precedence, otherwise we replay the snapshot.
        auto pending = std::move(pending_load_);
        pending_load_.reset();

        std::weak_ptr<bool> wp = alive_token_;
        if (on_event_) {
            on_event_(LifecycleEvent{
                LifecycleEvent::Kind::RendererReady, S_OK, 0});
        }
        if (!wp.lock()) {
            // Event handler destroyed *this. Bail before touching members.
            return;
        }
        if (pending_load_.has_value()) {
            DocumentRequest req = std::move(*pending_load_);
            pending_load_.reset();
            load_document(std::move(req));
        } else if (pending.has_value()) {
            load_document(std::move(*pending));
        }
    }
}

void ViewerHost::dispatch_process_failed(int process_failed_kind) {
    if (state_ == State::Closed) return;
    state_ = State::Crashed;
    pending_load_.reset();
    debug_log::log(L"viewer-host: renderer crashed, kind={}",
                   process_failed_kind);
    if (on_event_) {
        on_event_(LifecycleEvent{
            LifecycleEvent::Kind::RendererCrashed, S_OK,
            process_failed_kind});
    }
}

}
