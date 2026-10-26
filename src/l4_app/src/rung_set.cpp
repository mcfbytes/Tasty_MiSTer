// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rung_set.h"

#include <type_traits>

namespace mister::app {

template <class Self>
auto RungSet::rung_in_(Self& self, RungSlot slot) noexcept {
    using R = std::conditional_t<std::is_const_v<Self>, const Rung, Rung>;
    const auto live = [](auto& o) noexcept {
        return o ? infra::OptRef<R>{*o} : infra::OptRef<R>{};
    };
    switch (slot) {
        case RungSlot::Addons:
            return live(self.addons);
        case RungSlot::Pieces:
            return live(self.pieces);
        case RungSlot::Load:
            return live(self.load);
        case RungSlot::Manifest:
            return live(self.manifest);
        case RungSlot::Walk:
            return live(self.walk);
        case RungSlot::Companion:
            return live(self.companion_walk);
        case RungSlot::BootLadder:
        case RungSlot::SavestateHold:
        case RungSlot::SaveMount:
        case RungSlot::AddonsAfter:
        case RungSlot::Watch:
        case RungSlot::kCount:
            break;
    }
    return infra::OptRef<R>{};
}

infra::OptRef<Rung> RungSet::rung(RungSlot slot) noexcept {
    TASTY_SEAT_BODY(RungSet);
    return rung_in_(*this, slot);
}

infra::OptRef<const Rung> RungSet::rung(RungSlot slot) const noexcept {
    TASTY_SEAT_BODY(RungSet);
    return rung_in_(*this, slot);
}

RungMask RungSet::live() const noexcept {
    TASTY_SEAT_BODY(RungSet);
    RungMask m{};
    if (addons)
        m.set(RungSlot::Addons);
    if (addons_after)
        m.set(RungSlot::AddonsAfter);
    if (ss_hold)
        m.set(RungSlot::SavestateHold);
    if (pieces)
        m.set(RungSlot::Pieces);
    if (load)
        m.set(RungSlot::Load);
    if (manifest)
        m.set(RungSlot::Manifest);
    if (walk)
        m.set(RungSlot::Walk);
    if (companion_walk)
        m.set(RungSlot::Companion);
    return m;
}

}
