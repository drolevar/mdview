#pragma once

namespace mdview {

// Virtual host names for SetVirtualHostNameToFolderMapping.
// Renderer code lives at https://mdview-app.example/ (set once at navigate);
// document-relative resources resolve via https://mdview-doc.example/
// (remapped per loadDocument to the document's parent directory).
//
// .example is reserved by RFC 2606 / RFC 6761 for tooling and never
// resolves in real DNS. .local would route through mDNS first and
// adds a multi-second startup delay (see WebView2Feedback #1862).
inline constexpr const wchar_t* kAppHostName = L"mdview-app.example";
inline constexpr const wchar_t* kDocHostName = L"mdview-doc.example";
inline constexpr const wchar_t* kDocBaseUri  = L"https://mdview-doc.example/";

}
