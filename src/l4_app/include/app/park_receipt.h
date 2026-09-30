// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

class ParkReceipt {
public:
    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

private:
    friend class QuiesceChannel;
    explicit ParkReceipt(std::uint32_t at_gen) noexcept : gen_(at_gen) {}

    std::uint32_t gen_;
};

}  // namespace mister::app
