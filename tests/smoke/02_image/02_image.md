# mdview Smoke Test 02 — Relative Image

The image below should render via the per-load `mdview-doc.example`
virtual host remap.

![mdview logo](./logo.png)

If the logo appears: relative-image loading via `mdview-doc.example`
works.
If the image is broken: the virtual host mapping isn't being remapped
to the document's directory before the renderer parses the markdown.
