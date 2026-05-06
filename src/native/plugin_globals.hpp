#pragma once

#include "common/version.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

namespace mdview {

class PluginGlobals {
public:
    PluginGlobals() = default;

    PluginGlobals(const PluginGlobals&) = delete;
    PluginGlobals& operator=(const PluginGlobals&) = delete;

    void set_default_params(std::uint32_t hi, std::uint32_t low, std::string default_ini_name);
    void set_module_handle(HMODULE module_handle);

    bool is_initialized() const noexcept { return params_initialized_; }
    PluginInterfaceVersion interface_version() const noexcept { return version_; }
    const std::string& default_ini_name() const noexcept { return default_ini_name_; }

    HMODULE module_handle() const noexcept { return module_handle_; }
    std::filesystem::path module_directory() const;

    // Minimum plugin interface version required for ListLoadNextW.
    // Reconcile with external/WLX_SDK_NOTES.md before M3.
    static constexpr PluginInterfaceVersion list_load_next_threshold{2, 0};

    bool supports_list_load_next() const noexcept {
        return params_initialized_ && version_.at_least(list_load_next_threshold);
    }

private:
    bool params_initialized_ = false;
    PluginInterfaceVersion version_{0, 0};
    std::string default_ini_name_;
    HMODULE module_handle_ = nullptr;
};

// Process-wide instance, populated by ListSetDefaultParams and DllMain.
PluginGlobals& globals() noexcept;

}
