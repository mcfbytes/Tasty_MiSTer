// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

class ParkReceipt {
public:
    ParkReceipt(const ParkReceipt&) = delete;
    ParkReceipt& operator=(const ParkReceipt&) = delete;
    ParkReceipt(ParkReceipt&&) noexcept = default;
    ParkReceipt& operator=(ParkReceipt&&) noexcept = default;
    ~ParkReceipt() = default;

    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

private:
    friend class SwitchPark;

    explicit ParkReceipt(std::uint32_t at_gen) noexcept : gen_(at_gen) {}

    std::uint32_t gen_;
};

}
