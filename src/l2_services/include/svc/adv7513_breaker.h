// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/i2c_adapter.h"

namespace mister::svc::adv7513 {

class I2cBreaker final : public II2cAdapter {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint32_t kTripAfter = 3;

    static constexpr std::uint8_t kOpWrite = 0x80;

    struct Failure {
        std::uint8_t op = 0;
        os::I2cReg reg{};
        std::uint16_t err = 0;
    };

    explicit I2cBreaker(II2cAdapter& inner) noexcept : inner_(&inner) {}

    I2cBreaker(const I2cBreaker&) = delete;
    I2cBreaker& operator=(const I2cBreaker&) = delete;

    [[nodiscard]] Ex<std::uint8_t> probe(unsigned bus, os::I2cAddr addr) override;
    [[nodiscard]] Ex<void> open(unsigned bus, os::I2cAddr addr) override;
    [[nodiscard]] Ex<std::uint8_t> read(os::I2cAddr addr, os::I2cReg reg) override;
    [[nodiscard]] Ex<void> write(os::I2cAddr addr, os::I2cReg reg, std::uint8_t val) override;

    void reset() noexcept;

    void hold() noexcept { open_ = true; }

    bool tripped() const noexcept { return open_; }

    std::uint32_t failures() const noexcept { return failures_; }

    std::uint32_t trips() const noexcept { return trips_; }

    std::uint32_t refused() const noexcept { return refused_; }

    const Failure& first() const noexcept { return first_; }

    std::uint16_t last_errno() const noexcept { return last_errno_; }

private:
    void note(bool ok, std::uint8_t op, os::I2cReg reg, const Error* e) noexcept;

    II2cAdapter* inner_;
    bool open_ = false;
    std::uint32_t run_ = 0;
    std::uint32_t failures_ = 0;
    std::uint32_t trips_ = 0;
    std::uint32_t refused_ = 0;
    Failure first_{};
    std::uint16_t last_errno_ = 0;
};

}
