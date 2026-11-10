// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"

namespace mister::svc {

class OsdCombo {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Claim {
        bool named = false;
        bool pressed = false;
    };

    struct Claims {
        Claim escape{};
        Claim ok{};
    };

    enum class Turn { none, made, broken };

    [[nodiscard]] constexpr Turn update(const Claims& claims) noexcept {
        const bool was = held();
        escape_held_ = next(escape_held_, claims.escape);
        ok_held_ = next(ok_held_, claims.ok);
        if (held() && !was)
            return Turn::made;
        if (was && !held())
            return Turn::broken;
        return Turn::none;
    }

    [[nodiscard]] constexpr bool held() const noexcept { return escape_held_ && ok_held_; }

private:
    [[nodiscard]] static constexpr bool next(bool held_before, Claim claim) noexcept {
        return claim.named ? claim.pressed : held_before;
    }

    bool escape_held_ = false;
    bool ok_held_ = false;
};

static_assert(
    [] {
        constexpr OsdCombo::Claim press{true, true};
        constexpr OsdCombo::Claim release{true, false};
        constexpr OsdCombo::Claim quiet{};
        OsdCombo combo;
        if (combo.update({press, quiet}) != OsdCombo::Turn::none)
            return false;
        if (combo.update({quiet, press}) != OsdCombo::Turn::made)
            return false;
        if (combo.update({press, press}) != OsdCombo::Turn::none)
            return false;
        if (combo.update({release, press}) != OsdCombo::Turn::broken)
            return false;
        if (combo.update({quiet, press}) != OsdCombo::Turn::none)
            return false;
        if (combo.update({press, press}) != OsdCombo::Turn::made)
            return false;
        return true;
    }(),
    "OsdCombo::update: the 0 <-> both edge is the only edge"
);

}
