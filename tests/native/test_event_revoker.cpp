#include "native/event_revoker.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("EventRevoker calls remover on destruction", "[event_revoker]") {
    int call_count = 0;
    int last_token_value = 0;

    {
        EventRegistrationToken token{42};
        mdview::EventRevoker revoker(token,
            [&](EventRegistrationToken t) {
                ++call_count;
                last_token_value = static_cast<int>(t.value);
            });
        REQUIRE(call_count == 0);
    }

    REQUIRE(call_count == 1);
    REQUIRE(last_token_value == 42);
}

TEST_CASE("EventRevoker default-constructed is no-op", "[event_revoker]") {
    int call_count = 0;
    {
        mdview::EventRevoker revoker;
        REQUIRE(call_count == 0);
    }
    REQUIRE(call_count == 0);
}

TEST_CASE("EventRevoker move transfers ownership", "[event_revoker]") {
    int call_count = 0;
    EventRegistrationToken token{99};

    {
        mdview::EventRevoker first(token,
            [&](EventRegistrationToken) { ++call_count; });
        mdview::EventRevoker second = std::move(first);
        REQUIRE(call_count == 0);
    }

    REQUIRE(call_count == 1);
}
