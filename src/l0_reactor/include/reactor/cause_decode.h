// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reactor/cause_set.h"

namespace mister::reactor {

class CauseDecode {
public:
    [[nodiscard]] static constexpr CauseDecode idle() noexcept {
        return CauseDecode{Cause::None, false};
    }

    [[nodiscard]] static constexpr CauseDecode refused() noexcept {
        return CauseDecode{Cause::None, true};
    }

    [[nodiscard]] static constexpr CauseDecode of(Cause c) noexcept {
        return CauseDecode{c, false};
    }

    [[nodiscard]] constexpr Cause cause() const noexcept { return cause_; }

    [[nodiscard]] constexpr bool is_refused() const noexcept { return refused_; }

    friend constexpr bool operator==(CauseDecode, CauseDecode) = default;

private:
    constexpr CauseDecode(Cause c, bool refused) noexcept : cause_(c), refused_(refused) {}

    Cause cause_;
    bool refused_;
};

}
