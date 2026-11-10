// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "infra/opt_ref.h"
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

    infra::OptRef<const OptionItem> row() const noexcept { return row_; }

    infra::OptRef<const OptionItem> disc_row() const noexcept { return disc_row_; }

private:
    infra::OptRef<const OptionItem> row_{};
    infra::OptRef<const OptionItem> disc_row_{};
};

}
