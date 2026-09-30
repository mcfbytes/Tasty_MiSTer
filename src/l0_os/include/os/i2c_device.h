// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "infra/seat.h"

namespace mister::os {

class I2cDevice {
    TASTY_SEAT_EXEMPT(component);

public:
    static Ex<I2cDevice> open(unsigned bus, std::uint8_t addr);

    Ex<std::uint8_t> read_reg(std::uint8_t reg);
    Ex<void> write_reg(std::uint8_t reg, std::uint8_t value);
    Ex<void> read_block(std::uint8_t reg, std::span<std::uint8_t> out);

    Ex<std::uint16_t> read_word(std::uint8_t reg);

    Ex<std::uint8_t> read_byte();

private:
    I2cDevice() = default;
    UniqueFd fd_;
};

}  // namespace mister::os
