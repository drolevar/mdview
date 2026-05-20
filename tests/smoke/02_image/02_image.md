# mdview Smoke Test 02 - Relative Image

The image below should render via the per-load /doc/ route under
the single mdview.example origin.

![mdview logo](./logo.png)

If the logo appears: relative-image loading via the asset router's
/doc/ handler works.
If the image is broken: the asset router's current doc dir isn't
being set to the document's directory before the renderer parses
the markdown.
