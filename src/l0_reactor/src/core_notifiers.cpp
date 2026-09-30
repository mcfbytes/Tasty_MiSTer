// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/core_notifiers.h"

#include <utility>

namespace mister::reactor {

Ex<NotifierSlot> CoreNotifiers::bind_doorbell(
    os::UioHandle uio, Cause klass, std::optional<hal::RegisterWindow<hal::CauseReg>> window) {

    if (exec_ == nullptr) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    std::size_t i = 0;
    while (i < kMaxLines && entries_[i].live)
        ++i;
    if (i == kMaxLines) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(kMaxLines)});
    }

    Entry& e = entries_[i];
    e.window = std::move(window);
    const hal::RegisterWindow<hal::CauseReg>* cw = e.window.has_value() ? &*e.window : nullptr;
    auto n = Notifier::doorbell_adopt(std::move(uio), cw, klass);
    if (!n) {
        e.window.reset();
        return std::unexpected(n.error());
    }
    auto s = exec_->add_notifier(std::move(*n));
    if (!s) {
        e.window.reset();
        return std::unexpected(s.error());
    }
    e.slot = *s;
    e.live = true;
    ++live_;
    return *s;
}

void CoreNotifiers::release() noexcept {
    for (Entry& e : entries_) {
        if (!e.live) continue;

        if (exec_ != nullptr) (void)exec_->remove_notifier(e.slot);
        e.live = false;
        e.window.reset();
        --live_;
    }
}

}  // namespace mister::reactor
