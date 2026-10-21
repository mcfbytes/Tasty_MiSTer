// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ui/types.h"
#include "infra/seat.h"

namespace mister::ui {

inline constexpr unsigned kDebounceMs = 20;
inline constexpr unsigned kRepeatDelayMs = 500;
inline constexpr unsigned kRepeatRateMs = 50;
inline constexpr unsigned kMenuLongPressMs = 3000;
inline constexpr unsigned kUserLongPressMs = 1500;

class Navigator {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class Direction : std::uint8_t { Up, Down };

    void set_mask(MenuMask mask) noexcept { mask_ = mask; }

    MenuMask mask() const noexcept { return mask_; }

    void set_index(RowIndex i) noexcept { index_ = i; }

    RowIndex index() const noexcept { return index_; }

    bool move(Direction d) noexcept;

    bool selectable(RowIndex i) const noexcept;

private:
    MenuMask mask_{};
    RowIndex index_{};
};

}
