// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "ui/types.h"
#include "infra/seat.h"

namespace mister::ui {

inline constexpr std::chrono::milliseconds kDebounce{20};
inline constexpr std::chrono::milliseconds kRepeatDelay{500};
inline constexpr std::chrono::milliseconds kRepeatRate{50};

class Navigator {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class Direction : std::uint8_t { Up, Down };

    void set_mask(MenuMask mask) noexcept { mask_ = mask; }

    MenuMask mask() const noexcept { return mask_; }

    void set_index(RowIndex i) noexcept { index_ = i; }

    RowIndex index() const noexcept { return index_; }

    bool move(Direction d) noexcept;

    bool move_tier(Direction d, MenuMask run) noexcept;

    bool move_strip(Direction d, MenuMask run) noexcept;

    bool selectable(RowIndex i) const noexcept;

private:
    MenuMask mask_{};
    RowIndex index_{};
};

}
