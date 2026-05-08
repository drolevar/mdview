# Smoke fixtures

Markdown files exercised by the integration harness
(`mdview_integration_tests`) and by manual smoke runs against an installed
plugin.

Numbering convention:

- `01`–`07`: Migrated from `D:\Projects\mdview\smoke\` (parent-of-repo)
  during M4. Cover M1–M3 features.
- `08`–`10`: Mermaid (M4).
- `11`: highlight.js (M4).
- `12`–`13`: Theme (M4).
- `14`: Focus signal (M4 audit item 8).

The integration harness reads the directory at `MDVIEW_SMOKE_DIR`,
defaulting to `<repo>/src/tests/smoke` (this directory).
