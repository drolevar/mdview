#pragma once

#include <windows.h>

#include <filesystem>

namespace mdview {

// Returns the directory containing the module identified by `module_handle`.
// Pass nullptr to query the current process executable's directory.
// Throws std::runtime_error if the path cannot be retrieved.
std::filesystem::path module_directory(HMODULE module_handle);

}
