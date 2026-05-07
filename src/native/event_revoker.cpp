#include "native/event_revoker.hpp"

namespace mdview {

EventRevoker::EventRevoker(EventRegistrationToken token, Remover remover)
    : engaged_(true)
    , token_(token)
    , remover_(std::move(remover)) {
}

EventRevoker::~EventRevoker() {
    revoke();
}

EventRevoker::EventRevoker(EventRevoker&& other) noexcept
    : engaged_(other.engaged_)
    , token_(other.token_)
    , remover_(std::move(other.remover_)) {
    other.engaged_ = false;
}

EventRevoker& EventRevoker::operator=(EventRevoker&& other) noexcept {
    if (this != &other) {
        revoke();
        engaged_ = other.engaged_;
        token_   = other.token_;
        remover_ = std::move(other.remover_);
        other.engaged_ = false;
    }
    return *this;
}

void EventRevoker::revoke() noexcept {
    if (engaged_ && remover_) {
        remover_(token_);
    }
    engaged_ = false;
    remover_ = {};
}

}
