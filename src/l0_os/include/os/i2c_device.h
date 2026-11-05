// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class I2cDevice {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<I2cDevice> open(unsigned bus, I2cAddr addr);

    [[nodiscard]] Ex<std::uint8_t> read_reg(I2cReg reg);
    [[nodiscard]] Ex<void> write_reg(I2cReg reg, std::uint8_t value);
    [[nodiscard]] Ex<void> read_block(I2cReg reg, std::span<std::uint8_t> out);

    [[nodiscard]] Ex<std::uint16_t> read_word(I2cReg reg);

    [[nodiscard]] Ex<std::uint8_t> read_byte();

private:
    I2cDevice() = default;
    UniqueFd fd_;
};

}
