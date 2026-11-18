// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <utility>

#include "infra/seat.h"
#include "ui/page_id.h"
#include "ui/transition.h"

namespace mister::ui {

template <class Req>
struct LaunchSlot {
    TASTY_SEAT_RESIDENT(Ui);
    std::optional<Req> pending{};

    [[nodiscard]] Transition open(PageId page, Req req, bool replace = false) {
        (void)arm(std::move(req));
        return replace ? Transition::replace(page) : Transition::push(page);
    }

    std::optional<Req> take() noexcept {
        std::optional<Req> out = std::move(pending);
        pending.reset();
        return out;
    }

    void disarm() noexcept { pending.reset(); }

    [[nodiscard]] bool armed() const noexcept { return pending.has_value(); }

private:
    Req& arm(Req req) noexcept {
        pending = std::move(req);
        return *pending;
    }
};

}
