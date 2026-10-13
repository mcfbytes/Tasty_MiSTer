// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/core_notifiers.h"

#include <utility>

namespace mister::reactor {

Ex<NotifierSlot> CoreNotifiers::bind_doorbell(
    os::UioLineRef uio, Cause klass,
    infra::OptRef<const hal::RegisterWindow<hal::CauseReg>> window) {
    std::size_t i = 0;
    while (i < kMaxLines && entries_[i].live)
        ++i;
    if (i == kMaxLines) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(kMaxLines)});
    }
    const hal::RegisterWindow<hal::CauseReg>* cause = window ? &*window : nullptr;

    auto n = Notifier::doorbell_adopt(uio, cause, klass);
    if (!n) return std::unexpected(n.error());
    auto s = exec_.add_notifier(std::move(*n));
    if (!s) return std::unexpected(s.error());
    Entry& e = entries_[i];
    e.slot = *s;
    e.live = true;
    ++live_;
    return *s;
}

void CoreNotifiers::release() noexcept { retire_(false); }

void CoreNotifiers::retry_release() noexcept { retire_(true); }

void CoreNotifiers::retire_(bool refused_only) noexcept {
    for (Entry& e : entries_) {
        if (!e.live) continue;
        if (refused_only && !e.refused) continue;
        if (!exec_.remove_notifier(e.slot)) {
            if (release_failures_ != 0xFFFF'FFFFu) ++release_failures_;
            e.refused = true;
            continue;
        }
        e.live = false;
        e.refused = false;
        --live_;
    }
}

}  // namespace mister::reactor
