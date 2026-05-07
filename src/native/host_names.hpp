#pragma once

namespace mdview {

// Virtual host names for SetVirtualHostNameToFolderMapping.
// Renderer code lives at https://mdview-app.local/ (set once at navigate);
// document-relative resources resolve via https://mdview-doc.local/
// (remapped per loadDocument to the document's parent directory).
inline constexpr const wchar_t* kAppHostName = L"mdview-app.local";
inline constexpr const wchar_t* kDocHostName = L"mdview-doc.local";
inline constexpr const wchar_t* kDocBaseUri  = L"https://mdview-doc.local/";

}
