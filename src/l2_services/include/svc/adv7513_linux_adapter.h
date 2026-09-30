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

    Ex<std::uint8_t> probe(unsigned bus, std::uint8_t addr) override;
    Ex<void> open(unsigned bus, std::uint8_t addr) override;
    Ex<std::uint8_t> read(std::uint8_t addr, std::uint8_t reg) override;
    Ex<void> write(std::uint8_t addr, std::uint8_t reg, std::uint8_t val) override;

private:
    struct Slot {
        std::uint8_t addr = 0;

        std::optional<os::I2cDevice> dev{};
    };
    Slot* find(std::uint8_t addr) noexcept;

    std::array<Slot, 4> slots_{};
};

}  // namespace mister::svc::adv7513
