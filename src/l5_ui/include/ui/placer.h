// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "infra/seat.h"
#include "ui/core_verbs.h"
#include "ui/game_slot.h"
#include "ui/place_facts.h"
#include "ui/placement.h"

namespace mister::cores {
struct FileSlot;
}

namespace mister::ui {

class OptionItem;
class OptionsModel;

class Placer final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    Placer(
        const OptionsModel& walk,
        std::span<const cores::FileSlot> slots,
        const CoreVerbs& verbs,
        PlaceFacts facts
    );

    [[nodiscard]] Placement of(const OptionItem& item) const;

    const GameSlot& game() const noexcept { return game_; }

    std::span<const OptionItem* const> slots() const noexcept { return slots_; }

private:
    struct Placed {
        const OptionItem* item;
        Placement placement;
    };

    [[nodiscard]] Placement classify_(
        const OptionItem& item,
        const CoreVerbs& verbs,
        PlaceFacts facts
    ) const;

    [[nodiscard]] bool resets_the_game_(const OptionItem& item) const;

    GameSlot game_;
    std::vector<Placed> placed_;
    std::vector<const OptionItem*> slots_;
};

}
