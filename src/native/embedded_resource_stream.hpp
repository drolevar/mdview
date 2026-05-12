#pragma once

#include <wrl/implements.h>
#include <objidl.h>

#include <cstddef>

namespace mdview {

// Zero-copy IStream over a pointer + size, intended for wrapping a
// Win32 RT_RCDATA resource memory block returned by LockResource.
// The resource memory is owned by the module (our pinned DLL); the
// stream keeps only a raw pointer and never frees.
//
// Construct via Microsoft::WRL::MakeAndInitialize:
//   ComPtr<EmbeddedResourceStream> s;
//   THROW_IF_FAILED(Microsoft::WRL::MakeAndInitialize<EmbeddedResourceStream>(
//       &s, data, size));
//
// Read/Seek/Stat are implemented; everything else returns E_NOTIMPL
// or a sensible no-op. Not thread-safe — WebView2 dispatches a given
// stream's reads from a single thread.
class EmbeddedResourceStream
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IStream> {
public:
    HRESULT RuntimeClassInitialize(const std::byte* data,
                                   std::size_t       size) noexcept;

    // ISequentialStream
    IFACEMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead)  override;
    IFACEMETHODIMP Write(const void*, ULONG, ULONG*)         override
        { return STG_E_ACCESSDENIED; }

    // IStream
    IFACEMETHODIMP Seek(LARGE_INTEGER move, DWORD origin,
                        ULARGE_INTEGER* new_pos)             override;
    IFACEMETHODIMP Stat(STATSTG* stat, DWORD grfStatFlag)    override;
    IFACEMETHODIMP SetSize(ULARGE_INTEGER)                   override
        { return E_NOTIMPL; }
    IFACEMETHODIMP CopyTo(IStream*, ULARGE_INTEGER,
                          ULARGE_INTEGER*, ULARGE_INTEGER*)  override
        { return E_NOTIMPL; }
    IFACEMETHODIMP Commit(DWORD)                             override
        { return S_OK; }
    IFACEMETHODIMP Revert()                                  override
        { return E_NOTIMPL; }
    IFACEMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                              DWORD)                         override
        { return E_NOTIMPL; }
    IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                DWORD)                       override
        { return E_NOTIMPL; }
    IFACEMETHODIMP Clone(IStream**)                          override
        { return E_NOTIMPL; }

private:
    const std::byte* data_ = nullptr;
    std::size_t      size_ = 0;
    std::size_t      pos_  = 0;
};

}  // namespace mdview
