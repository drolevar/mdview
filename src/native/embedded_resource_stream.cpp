#include "native/embedded_resource_stream.hpp"

#include <algorithm>
#include <cstring>

namespace mdview {

HRESULT EmbeddedResourceStream::RuntimeClassInitialize(
        const std::byte* data, std::size_t size) noexcept {
    if (data == nullptr && size > 0) return E_INVALIDARG;
    data_ = data;
    size_ = size;
    pos_  = 0;
    return S_OK;
}

IFACEMETHODIMP EmbeddedResourceStream::Read(
        void* pv, ULONG cb, ULONG* pcbRead) {
    if (pv == nullptr) {
        if (pcbRead) *pcbRead = 0;
        return STG_E_INVALIDPOINTER;
    }
    const std::size_t remaining = size_ - pos_;
    const ULONG to_read = static_cast<ULONG>(
        std::min<std::size_t>(cb, remaining));
    if (to_read > 0) {
        std::memcpy(pv, data_ + pos_, to_read);
        pos_ += to_read;
    }
    if (pcbRead) *pcbRead = to_read;
    // Spec: S_OK if cb bytes read, S_FALSE if fewer (EOF reached).
    return (to_read == cb) ? S_OK : S_FALSE;
}

IFACEMETHODIMP EmbeddedResourceStream::Seek(
        LARGE_INTEGER move, DWORD origin,
        ULARGE_INTEGER* new_pos) {
    int64_t base = 0;
    switch (origin) {
        case STREAM_SEEK_SET: base = 0; break;
        case STREAM_SEEK_CUR:
            base = static_cast<int64_t>(pos_);
            break;
        case STREAM_SEEK_END:
            base = static_cast<int64_t>(size_);
            break;
        default: return STG_E_INVALIDFUNCTION;
    }
    const int64_t target = base + move.QuadPart;
    if (target < 0 ||
        target > static_cast<int64_t>(size_)) {
        return STG_E_INVALIDFUNCTION;
    }
    pos_ = static_cast<std::size_t>(target);
    if (new_pos) new_pos->QuadPart = pos_;
    return S_OK;
}

IFACEMETHODIMP EmbeddedResourceStream::Stat(
        STATSTG* stat, DWORD /*grfStatFlag*/) {
    if (stat == nullptr) return STG_E_INVALIDPOINTER;
    *stat = STATSTG{};
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = size_;
    // pwcsName left null; STATFLAG_NONAME consumers (WebView2's
    // CreateWebResourceResponse path included) don't need it.
    return S_OK;
}

}  // namespace mdview
