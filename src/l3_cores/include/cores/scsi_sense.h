// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/seat.h"

namespace mister::cores {

enum class SenseKey : std::uint8_t { NoSense = 0x00, NotReady = 0x02, IllegalRequest = 0x05 };

enum class Asc : std::uint8_t { None = 0x00, NoDisc = 0x0B, InvalidCommand = 0x20 };

[[nodiscard]] constexpr std::byte sense_key_byte(SenseKey key) noexcept {
    return static_cast<std::byte>(key);
}

[[nodiscard]] constexpr std::byte asc_byte(Asc asc) noexcept {
    return static_cast<std::byte>(asc);
}

class ScsiSense {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Block {
        SenseKey key = SenseKey::NoSense;
        Asc asc = Asc::None;
    };

    void raise(SenseKey key, Asc asc) noexcept { block_ = Block{key, asc}; }

    [[nodiscard]] Block take() noexcept {
        const Block out = block_;
        block_ = Block{};
        return out;
    }

private:
    Block block_{};
};

}
