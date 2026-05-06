#include "native/plugin_globals.hpp"

#include "common/paths.hpp"

namespace mdview {

namespace {
PluginGlobals g_instance;
}

void PluginGlobals::set_default_params(std::uint32_t hi, std::uint32_t low, std::string default_ini_name) {
    version_.hi = hi;
    version_.low = low;
    default_ini_name_ = std::move(default_ini_name);
    params_initialized_ = true;
}

void PluginGlobals::set_module_handle(HMODULE module_handle) {
    module_handle_ = module_handle;
}

std::filesystem::path PluginGlobals::module_directory() const {
    return mdview::module_directory(module_handle_);
}

PluginGlobals& globals() noexcept {
    return g_instance;
}

}
