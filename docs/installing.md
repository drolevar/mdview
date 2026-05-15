# Installing

## Requirements

- **Total Commander**, 64-bit.
- **Microsoft Edge WebView2 Runtime.** Preinstalled on current
  Windows 11. If a `.md` file opens to an error about the runtime,
  install the Evergreen runtime from
  <https://developer.microsoft.com/microsoft-edge/webview2/> and try
  again.

## Recommended: drag-and-drop install

1. Download the latest `mdview-X.Y.Z.zip` from the
   [Releases](https://github.com/drolevar/mdview/releases) page.
2. In Total Commander, open the `.zip` (Ctrl+PageDown on it) or drag
   it onto a panel.
3. Total Commander detects the bundled `pluginst.inf` and prompts to
   install the plugin. Accept.
4. Total Commander copies `mdview.wlx64` into its plugin directory
   and registers it as a Lister plugin for `.md`, `.markdown`,
   `.mdown`, and `.mkd` files.

Press **F3** on any Markdown file (or use Quick View, Ctrl+Q).

The install location is whatever you have configured as Total
Commander's plugin directory; `pluginst.inf` only specifies the
`mdview` subfolder name, not the parent path.

## Manual install

If you build from source or prefer manual setup:

1. Copy `mdview.wlx64` into a folder under Total Commander's plugin
   directory, e.g. `…\plugins\wlx\mdview\mdview.wlx64`.
2. In Total Commander: **Configuration → Options → Edit/View/Search
   → Lister plugins**, add `mdview.wlx64`.
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
