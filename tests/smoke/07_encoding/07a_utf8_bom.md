# mdview Smoke Test 07a — UTF-8 (no BOM)

This file is encoded in UTF-8 **with    ** a byte-order mark.

Non-ASCII characters:

- Russian: Привет, мир
- Korean: 안녕하세요
- Greek: γειά σου κόσμε
- Chinese: 你好世界
- Emoji: 🌍 🎉 ✅

If all the lines above render correctly, no-BOM UTF-8 strict decoding
works (the `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` path).
