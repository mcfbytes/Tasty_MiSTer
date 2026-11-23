// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/akiko_prefetch.h"

#include <algorithm>

namespace mister::cores::minimig::akiko {

void AkikoPrefetch::invalidate() noexcept {
    TASTY_SEAT_BODY(AkikoPrefetch);
    for (Window& w : *windows_)
        w.base.reset();
}

std::optional<AkikoPrefetch::WindowIndex> AkikoPrefetch::find_(proto::Lba lba) const noexcept {
    for (std::uint8_t i = 0; i < kWindows; ++i) {
        if ((*windows_)[WindowIndex{i}].holds(lba))
            return WindowIndex{i};
    }
    return std::nullopt;
}

AkikoPrefetch::WindowIndex AkikoPrefetch::evict_() const noexcept {
    WindowIndex pick{};
    for (std::uint8_t i = 0; i < kWindows; ++i) {
        const Window& w = (*windows_)[WindowIndex{i}];
        if (!w.base)
            return WindowIndex{i};
        if (w.stamp < (*windows_)[pick].stamp)
            pick = WindowIndex{i};
    }
    return pick;
}

bool AkikoPrefetch::fill_(const CdDisc& disc, Window& w, proto::Lba base) noexcept {
    w.base = base;
    bool any = false;
    proto::Lba at = base;
    for (Window::Slot& slot : w.slots) {
        slot.valid = disc.read(at, slot.frame);
        any = any || slot.valid;
        at = at + proto::BlockCount{1};
    }
    return any;
}

AkikoPrefetch::Got AkikoPrefetch::get(const CdDisc& disc, proto::Lba lba, RawFrame out) noexcept {
    TASTY_SEAT_BODY(AkikoPrefetch);
    std::optional<WindowIndex> at = find_(lba);
    if (!at) {
        misses_.add();
        at = evict_();
        const proto::Lba base = proto::Lba{} + kSectors * ((lba - proto::Lba{}) / kSectors);
        if (!fill_(disc, (*windows_)[*at], base))
            return Got::FillFailed;
    } else {
        hits_.add();
    }
    Window& w = (*windows_)[*at];
    lru_ = lru_.next();
    w.stamp = lru_;
    Window::Slot& slot = w.slot(lba);
    if (!slot.valid) {
        slot.valid = disc.read(lba, slot.frame);
        if (!slot.valid)
            return Got::Unreadable;
    }
    std::ranges::copy(slot.frame, out.begin());
    return Got::Sector;
}

}
