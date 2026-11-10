// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/identity_latch.h"

namespace mister::app {

void IdentityLatch::publish(const SessionIdentity& id) noexcept {
    cell_.publish(id);
}

std::optional<SessionIdentity> IdentityLatch::copy() const noexcept {

    SessionIdentity scratch{};
    if (cell_.sample_into(scratch) == 0)
        return std::nullopt;
    return scratch;
}

}
