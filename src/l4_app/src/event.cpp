// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/event.h"

namespace mister::app {

bool EventQueue::push(const Event& e) noexcept {
    if (ring_.push(e)) return true;
    const auto idx = infra::ordinal(infra::kind_of(e));
    if (idx < kKinds) ++loss_[idx];
    return false;
}

std::uint32_t EventQueue::losses(Event::Kind k) const noexcept {
    const auto idx = infra::ordinal(k);
    return idx < kKinds ? loss_[idx] : 0u;
}

std::uint32_t EventQueue::losses(Delivery d) const noexcept {
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < kKinds; ++i) {
        if (delivery_of(static_cast<Event::Kind>(i)) == d) n += loss_[i];
    }
    return n;
}

}  // namespace mister::app
