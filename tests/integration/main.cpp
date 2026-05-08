#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <windows.h>

int wmain(int /*argc*/, wchar_t** /*argv*/) {
    // TODO Phase I6: replace with a custom session that pumps the
    // message loop, attaches a DebugMonitor, and emits case markers.
    return Catch::Session().run();
}

TEST_CASE("placeholder integration test", "[integration]") {
    REQUIRE(1 + 1 == 2);
}
