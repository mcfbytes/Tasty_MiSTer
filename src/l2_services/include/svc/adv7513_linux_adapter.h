// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/i2c_device.h"
#include "svc/i2c_adapter.h"

namespace mister::svc::adv7513 {

class LinuxAdapter final : public II2cAdapter {
    TASTY_SEAT_RESIDENT(Ui);

public:
    LinuxAdapter() = default;
    LinuxAdapter(const LinuxAdapter&) = delete;
    LinuxAdapter& operator=(const LinuxAdapter&) = delete;

    [[nodiscard]] Ex<std::uint8_t> probe(unsigned bus, os::I2cAddr addr) override;
    [[nodiscard]] Ex<void> open(unsigned bus, os::I2cAddr addr) override;
    [[nodiscard]] Ex<std::uint8_t> read(os::I2cAddr addr, os::I2cReg reg) override;
    [[nodiscard]] Ex<void> write(os::I2cAddr addr, os::I2cReg reg, std::uint8_t val) override;

private:
    struct Slot {
        os::I2cAddr addr{};

        std::optional<os::I2cDevice> dev{};
    };

    Slot* find(os::I2cAddr addr) noexcept;

    std::array<Slot, 4> slots_{};
};

}
