# mdview — Markdown viewer plugin for Total Commander

A WLX (Lister) plugin that previews Markdown files inside Total Commander, with Mermaid diagrams, scientific text, and syntax highlighting. Rendering is done by an embedded Microsoft Edge WebView2.

## Status

Early development. Only the M1 skeleton (loadable plugin DLL with a native "Loading…" splash) is implemented.

## Build

Requires Visual Studio 2026 (or any MSVC with C++23 support), CMake ≥ 3.27, and Ninja. vcpkg is included as a submodule.

```powershell
git clone --recurse-submodules https://github.com/drolevar/mdview.git
cd mdview
cmake --preset windows-msvc-x64-debug
cmake --build --preset windows-msvc-x64-debug
ctest --preset windows-msvc-x64-debug
```

## Install in Total Commander (development)

```powershell
.\tools\install-to-totalcmd.ps1 -BuildDir build\windows-msvc-x64-debug
```

Then in TC: *Configuration → Options → Edit/View/Search → Lister Plugins → Configure*.

## License

See `LICENSE`.
