<!-- Update this file for each vX.Y.Z BEFORE pushing the tag. The
     tag-triggered CI release job attaches it verbatim. Current: v0.13.0 / M13. -->
One zip installs mdview on **both 32- and 64-bit Total Commander**.

**Install:** drag `mdview-<version>.zip` into Total Commander (or
Ctrl+PageDown it) and accept the install prompt. TC auto-selects the
binary matching its bitness (`mdview.wlx` 32-bit / `mdview.wlx64`
64-bit) from the bundled `pluginst.inf`. Then press **F3** on any
`.md` / `.markdown` / `.mdown` / `.mkd` file.

**Contents:** `mdview.wlx`, `mdview.wlx64`, `pluginst.inf`, `README.txt`.

**Requires:** the Microsoft Edge WebView2 runtime (preinstalled on
current Windows 11; otherwise install the Evergreen runtime).

Built, tested, and packaged by CI with full x86 + x64 parity
(both architectures hard-gated). See `docs/installing.md` for manual
install and details.
