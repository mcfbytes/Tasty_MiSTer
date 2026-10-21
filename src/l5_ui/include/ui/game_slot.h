// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "infra/seat.h"

namespace mister::cores {
struct FileSlot;
}

namespace mister::ui {

class OptionItem;
class OptionsModel;

class GameSlot final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    GameSlot(const OptionsModel& root, std::span<const cores::FileSlot> slots);

    const OptionItem* row() const noexcept { return row_; }

    const OptionItem* disc_row() const noexcept { return disc_row_; }

private:
    const OptionItem* row_;
    const OptionItem* disc_row_;
};

}
