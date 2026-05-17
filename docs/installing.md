# Installing

## Requirements

- **Total Commander**, 32-bit or 64-bit.
- **Microsoft Edge WebView2 Runtime.** If a `.md` file opens to an error about the runtime,
  install the WebView2 runtime from
  <https://developer.microsoft.com/microsoft-edge/webview2/> and try
  again.
- **No Visual C++ redistributable** is required; the C/C++
  runtime is linked statically into the plugin.
- **Windows 7 SP1 / 8.1** are supported, but only with the last
  WebView2 runtime they can install (v109). That runtime has
  received no security updates since October 2023; use it at
  your own risk.

## Recommended: drag-and-drop install

1. Download the latest `mdview-X.Y.Z.zip` from the
   [Releases](https://github.com/drolevar/mdview/releases) page.
2. In Total Commander, open the `.zip`.
3. Total Commander detects the bundled `pluginst.inf` and prompts to
   install the plugin. Accept.
4. Total Commander copies the matching binary into its plugin
   directory and registers it as a Lister plugin for `.md`,
   `.markdown`, `.mdown`, and `.mkd` files. The zip ships both
   `mdview.wlx` (32-bit) and `mdview.wlx64` (64-bit); `pluginst.inf`
   declares `file=mdview.wlx` and Total Commander appends `64`
   automatically when running 64-bit, so the right binary is
   selected for you.

Press **F3** on any Markdown file (or use Quick View, Ctrl+Q).

The install location is whatever you have configured as Total
Commander's plugin directory; `pluginst.inf` only specifies the
`mdview` subfolder name, not the parent path.

## Manual install

If you build from source or prefer manual setup:

1. Copy the binary matching your Total Commander's bitness into a
   folder under its plugin directory, e.g.
   `…\plugins\wlx\mdview\mdview.wlx64`. Use `mdview.wlx64` for
   64-bit Total Commander, `mdview.wlx` for 32-bit. (Copying both
   into the same folder is also fine — Total Commander loads the
   one matching its bitness.)
2. In Total Commander: **Configuration → Options → Edit/View/Search
   → Lister plugins**, add the file you copied (`mdview.wlx64` for
   64-bit, `mdview.wlx` for 32-bit).
3. Set its detect string to associate the Markdown extensions you
   want, e.g.:

   ```
   EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD"
   ```

`tools\build.ps1 release -Install` automates the copy step into
`%APPDATA%\GHISLER\plugins\wlx\mdview\` for development.

## Updating

Install a newer zip the same way; Total Commander overwrites the
existing plugin. Close any open Lister window (or restart Total
Commander) afterward so the new DLL is loaded — Total Commander
caches the loaded plugin per Lister.

## Uninstalling

Remove the `mdview` entry from **Lister plugins** in Total
Commander's configuration and delete the `mdview` folder from the
plugin directory.
