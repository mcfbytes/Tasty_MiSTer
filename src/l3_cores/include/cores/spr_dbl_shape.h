// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cores/i_rom_shape.h"
#include "infra/seat.h"

namespace mister::cores::neogeo {

class SprDblShape final : public IRomShape {
    TASTY_SEAT_RESIDENT(Main);

public:
    void transform(std::span<std::uint8_t> piece) noexcept override;

private:
    std::vector<std::uint16_t> src_;
    std::vector<std::uint16_t> out_;
};

}
