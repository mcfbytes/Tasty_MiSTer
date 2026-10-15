// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/event.h"

namespace mister::app {

template <SeatTag Producer>
bool EventQueue<Producer>::push(const Event& e) noexcept {

    if (ring_.push(e)) return true;
    const auto idx = infra::ordinal(infra::kind_of(e));
    if (idx < kKinds) loss_[idx].add();
    return false;
}

template <SeatTag Producer>
std::uint32_t EventQueue<Producer>::losses(Event::Kind k) const noexcept {
    const auto idx = infra::ordinal(k);
    return idx < kKinds ? loss_[idx].get() : 0u;
}

template <SeatTag Producer>
std::uint32_t EventQueue<Producer>::losses(Delivery d) const noexcept {
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < kKinds; ++i) {
        if (delivery_of(static_cast<Event::Kind>(i)) == d) n += loss_[i].get();
    }
    return n;
}

template class EventQueue<SeatTag::RT>;
template class EventQueue<SeatTag::Main>;

}  // namespace mister::app
