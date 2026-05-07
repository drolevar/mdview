#pragma once

#include <EventToken.h>

#include <functional>

namespace mdview {

// RAII wrapper around an EventRegistrationToken + paired remover.
// On destruction (or explicit revoke()) invokes the remover with the
// stored token. Move-only.
class EventRevoker {
public:
    using Remover = std::function<void(EventRegistrationToken)>;

    EventRevoker() noexcept = default;
    EventRevoker(EventRegistrationToken token, Remover remover);
    ~EventRevoker();

    EventRevoker(const EventRevoker&)            = delete;
    EventRevoker& operator=(const EventRevoker&) = delete;

    EventRevoker(EventRevoker&&) noexcept;
    EventRevoker& operator=(EventRevoker&&) noexcept;

    void revoke() noexcept;

private:
    bool                    engaged_ = false;
    EventRegistrationToken  token_{};
    Remover                 remover_;
};

}
