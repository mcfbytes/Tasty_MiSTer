// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/prelude.h"
#include "hal/board_link.h"

namespace mister::boards {

class RefusingBoardLink final : public hal::IBoardLink {
    TASTY_SEAT_RESIDENT(RT);

public:
    RefusingBoardLink() = default;

    [[nodiscard]] bool ready() const override;
    [[nodiscard]] hal::PinSample sample() const noexcept override;

    [[nodiscard]] Ex<hal::CoreTypeByte> identify() override;
    hal::CoreCapabilities latch_capabilities() override;
    void set_core_reset(bool asserted) override;

    void select(hal::ChipSelect cs) override;
    void deselect() override;
    [[nodiscard]] Ex<hal::LinkWord> transfer(hal::LinkWord out) override;
    [[nodiscard]] Ex<void> block_write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> block_read(std::span<std::uint8_t> data) override;
    [[nodiscard]] Ex<void> post(hal::LinkWord out) override;
    [[nodiscard]] Ex<bool> posted_done() override;
};

}
