// M13 precompiled header. SYSTEM / THIRD-PARTY headers ONLY.
// Never add project headers here: a TU that compiles only because
// the PCH supplied an include it didn't name is an include-hygiene
// bug that surfaces on the other arch or in a refactor.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wrl/client.h>

#include <WebView2.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
