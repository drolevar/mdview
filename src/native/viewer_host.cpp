#include "native/viewer_host.hpp"

#include "native/debug_log.hpp"
#include "native/host_names.hpp"
#include "native/renderer_protocol.hpp"
#include "native/theme.hpp"

#include <cassert>
#include <string>
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
    // Theme delivery: prefer pending (latest TC signal we haven't
    // pushed yet), then current (the most recently applied), else
    // System (renderer falls back to prefers-color-scheme).
    request.theme = pending_theme_.value_or(current_theme_);
    pending_theme_.reset();
    debug_log::log(L"viewer-host: load_document id={} file={}",
                   request.doc_id, request.file_path.wstring());
    if (!request.doc_dir.empty()) {
        const HRESULT hr = host_->remap_doc_dir(request.doc_dir);
        if (FAILED(hr)) {
            debug_log::log(
                L"viewer-host: remap failed hr=0x{:08X}",
                static_cast<uint32_t>(hr));
            request.base_uri.clear();
        } else {
            debug_log::log(
                L"viewer-host: remap ok dir={}",
                request.doc_dir.wstring());
            request.base_uri = kDocBaseUri;
        }
    }

    if (state_ == State::RendererReady || state_ == State::Loaded) {
        // Fast post path: skip the reload when the new doc won't
        // make any cross-origin fetches that depend on a fresh
        // navigation snapshot. Two cases:
        //   (a) same doc-dir as the loaded page (typical arrow-key
        //       navigation through .md files in one folder), and
        //   (b) no doc-dir at all (error stub, or a document with
        //       no relative resources).
        const bool can_fast_post =
            state_ == State::Loaded
            && (request.doc_dir.empty()
                || (!request.base_uri.empty()
                    && request.doc_dir == last_loaded_doc_dir_));
        if (can_fast_post) {
            post_request_(std::move(request));
            return;
        }

        // New doc-dir (or first user-driven load on this renderer):
        // mapping changes don't propagate to the current page's
        // resource loaders (WebView2 docs; WebView2Feedback #2456).
        // Reload to force a fresh navigation; replay the pending
        // load when the post-reload Ready arrives.
        pending_load_ = std::move(request);
        state_        = State::Navigated;
        debug_log::log(L"viewer-host: reloading for new doc-dir");
        host_->reload();
        return;
    }
    pending_load_ = std::move(request);   // latest-wins
}

void ViewerHost::apply_theme(Theme theme) {
    debug_log::log(L"viewer-host: apply_theme requested={}",
                   to_wire(theme));
    if (state_ == State::Failed
     || state_ == State::Crashed
     || state_ == State::Closed) {
        return;
    }

    current_theme_ = theme;

    // Before the renderer is ready, stash; the first loadDocument that
    // we eventually post carries the theme in its `theme` field.
    if (state_ != State::RendererReady && state_ != State::Loaded) {
        pending_theme_ = theme;
        return;
    }

    pending_theme_.reset();

    // Push the theme to the renderer immediately so cheap-and-instant
    // changes (CSS toggle, hljs stylesheet swap) happen now…
    post_set_theme_(theme);

    // …and re-issue the most recent loadDocument so mermaid SVGs
    // re-render with the new theme. Mermaid bakes theme into the SVG
    // at render time; toggling CSS won't recolor existing diagrams.
    if (state_ == State::Loaded && !last_loaded_doc_dir_.empty()) {
        if (on_event_) {
            on_event_(LifecycleEvent{
                LifecycleEvent::Kind::ThemeChanged, S_OK, 0});
        }
    }
}

void ViewerHost::post_set_theme_(Theme t) {
    std::wstring json = L"{\"type\":\"setTheme\",\"version\":1,\"theme\":\"";
    json.append(to_wire(t));
    json.append(L"\"}");
    if (host_) host_->post_to_renderer(json);
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
        auto snapshot = std::move(pending_load_);
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

        // If the callback called load_document, state moved out of
        // RendererReady (into Navigated, queued for reload). Don't
        // double-drain — the reload's own ready will handle it.
        if (state_ != State::RendererReady) {
            return;
        }

        // Drain the snapshot directly (not via load_document). The
        // current navigation is fresh and its loaders see the latest
        // mapping, so a reload here would be redundant. Going through
        // load_document would itself trigger reload because state is
        // RendererReady; that's correct for *new* user requests but
        // not for replaying a pre-ready queued load.
        if (snapshot.has_value()) {
            pending_load_ = std::move(snapshot);
            post_pending_directly_();
        }
    }
}

void ViewerHost::post_pending_directly_() {
    if (!pending_load_) return;
    DocumentRequest req = std::move(*pending_load_);
    pending_load_.reset();

    // Retry remap_doc_dir if the original attempt in load_document
    // ran before the host's WebView2 was ready (E_UNEXPECTED, leaves
    // base_uri empty). When this retry succeeds it means the current
    // page navigated with a stale (placeholder) doc-mapping; the
    // page's resource loaders are bound to the wrong folder. Reload
    // so the post-reload navigation picks up the just-set mapping;
    // the second drain finds base_uri non-empty and posts directly.
    bool remapped_at_drain = false;
    if (!req.doc_dir.empty() && req.base_uri.empty()) {
        const HRESULT hr = host_->remap_doc_dir(req.doc_dir);
        if (FAILED(hr)) {
            debug_log::log(
                L"viewer-host: remap (drain) failed hr=0x{:08X}",
                static_cast<uint32_t>(hr));
        } else {
            req.base_uri      = kDocBaseUri;
            remapped_at_drain = true;
        }
    }

    if (remapped_at_drain) {
        debug_log::log(L"viewer-host: reloading after late remap");
        pending_load_ = std::move(req);
        state_        = State::Navigated;
        host_->reload();
        return;
    }

    post_request_(std::move(req));
}

void ViewerHost::post_request_(DocumentRequest req) {
    LoadDocumentMessage msg;
    msg.id           = req.doc_id;
    msg.path         = req.file_path;
    msg.display_name = req.display_name;
    msg.base_uri     = req.base_uri;
    msg.markdown     = std::move(req.markdown);
    msg.options      = options_;
    msg.theme            = req.theme;
    msg.summary_requested = req.summary_requested;
    host_->post_to_renderer(encode_load_document(msg));
    state_               = State::Loaded;
    last_loaded_doc_dir_ = req.doc_dir;
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
