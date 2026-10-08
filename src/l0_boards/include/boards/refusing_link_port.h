// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/link_port.h"

namespace mister::boards {

class RefusingLinkPort final : public hal::ILinkPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    RefusingLinkPort() = default;

    [[nodiscard]] bool ready() const override;
    [[nodiscard]] hal::SpiSample sample() const noexcept override;

    [[nodiscard]] Ex<hal::CoreIdentity> identify() override;
    hal::CoreCapabilities latch_capabilities() override;
    void set_core_reset(bool asserted) override;

    void select(hal::ChipSelect cs) override;
    void deselect() override;
    [[nodiscard]] Ex<hal::SpiWord> transfer(hal::SpiWord out) override;
    [[nodiscard]] Ex<void> block_write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> block_read(std::span<std::uint8_t> data) override;
    [[nodiscard]] Ex<void> post(hal::SpiWord out) override;
    [[nodiscard]] Ex<bool> posted_done() override;
};

}  // namespace mister::boards
