// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/joystick.h"
#include "proto/link_pad_encoder.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

class PadLevels {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Reg {
        TASTY_SEAT_EXEMPT(const_shared);
        std::uint16_t opcode;
        std::uint8_t index;
    };

    static constexpr std::size_t kPorts = JoystickPort::kMaxPorts;
    static constexpr std::size_t kCount = 4 * kPorts;

    static const std::array<Reg, kCount> kRegs;

    [[nodiscard]] static constexpr std::optional<std::size_t> slot(
        std::uint16_t opcode,
        std::uint8_t index
    ) noexcept {
        for (std::size_t i = 0; i < kCount; ++i)
            if (kRegs[i].opcode == opcode && kRegs[i].index == index)
                return i;
        return std::nullopt;
    }

    [[nodiscard]] static constexpr std::uint32_t stick_word(
        std::uint8_t x,
        std::uint8_t y
    ) noexcept {
        return (static_cast<std::uint32_t>(y) << 8) | x;
    }

    void put(std::size_t slot, std::uint32_t word) noexcept { words_[slot] = word; }

    [[nodiscard]] std::optional<std::uint32_t> at(std::size_t slot) const noexcept {
        return words_[slot];
    }

    [[nodiscard]] Ex<void> replay(std::size_t slot, ILinkPadEncoder& to) const;

private:
    std::array<std::optional<std::uint32_t>, kCount> words_{};
};

inline constexpr std::array<PadLevels::Reg, PadLevels::kCount> PadLevels::kRegs = [] {
    std::array<Reg, kCount> r{};
    for (std::uint8_t p = 0; p < kPorts; ++p) {
        r[p] = Reg{JoystickPort::digital_opcode(p), 0};
        r[kPorts + p] = Reg{
            static_cast<std::uint16_t>(UioOp::Astick),
            JoystickPort::stick_index(PlayerIndex{p})
        };
        r[2 * kPorts + p] = Reg{
            static_cast<std::uint16_t>(UioOp::Astick),
            JoystickPort::paddle_index(PlayerIndex{p})
        };
        r[3 * kPorts + p] = Reg{
            static_cast<std::uint16_t>(UioOp::Astick2),
            JoystickPort::stick_index(PlayerIndex{p})
        };
    }
    return r;
}();

}
