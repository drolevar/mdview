# mdview Smoke Test 05a — First File

Open this file in TC's Lister, then press the **Down** arrow on the
panel side (with Quick View locked) so TC arrow-keys to
`05_second.md`. This exercises the `ListLoadNextW` reuse path: the
WebView2 controller is NOT recreated; only `loadDocument` is reposted.

DbgView (filtered to `[mdview]`) should show on **first open** of the
Lister:

```
[mdview] viewer-host create: env-pending
[mdview] env init starting; udf=...
[mdview] env ready
[mdview] viewer-host: controller-pending
[mdview] viewer-host: navigated
[mdview] navigation starting: https://mdview-app.local/index.html
[mdview] viewer-host: renderer ready
```

Then on the **arrow-through to 05_second.md**:

```
[mdview] (just a loadDocument post; no env-pending, no controller-pending)
```

The `id` field in the `loadDocument` JSON should increment from 1 to 2.
