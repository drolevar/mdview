#include "plugin/plugin_window.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

// Note: PluginWindow::send_command is exercised end-to-end by the
// integration harness (Phase J). The unit test here only validates
// that the bitmask logic in send_command compiles and the public
// signature is stable. Behaviour is observed via ViewerHost in J.

TEST_CASE("PluginWindow::send_command signature is stable",
          "[plugin_window]") {
    // No invocation; this is a compile-time anchor.
    using SendCmd = bool (mdview::PluginWindow::*)(int, int) noexcept;
    SendCmd p = &mdview::PluginWindow::send_command;
    REQUIRE(p != nullptr);
}
