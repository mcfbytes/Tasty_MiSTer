// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/joystick.h"
#include "proto/stick_pos.h"
#include "proto/link_pad_encoder.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

class PadLevels {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Slot {
        TASTY_SEAT_RESIDENT(RT);
        std::uint8_t v = 0;
        friend constexpr bool operator==(Slot, Slot) = default;
    };

    struct Reg {
        TASTY_SEAT_EXEMPT(const_shared);
        UioOp opcode;
        std::uint8_t index;
    };

    static constexpr std::size_t kPorts = JoystickPort::kMaxPorts;
    static constexpr std::size_t kCount = 4 * kPorts;

    static const std::array<Reg, kCount> kRegs;

    [[nodiscard]] static constexpr Slot slot_at(std::size_t i) noexcept {
        return Slot{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] static constexpr const Reg& reg(Slot slot) noexcept { return kRegs[slot.v]; }

    [[nodiscard]] static constexpr bool is_digital(Slot slot) noexcept { return slot.v < kPorts; }

    [[nodiscard]] static constexpr Slot not_a_level() noexcept { return slot_at(kCount); }

    [[nodiscard]] static constexpr bool is_level(Slot slot) noexcept { return slot.v < kCount; }

    [[nodiscard]] static constexpr Slot level_of(UioOp opcode, std::uint8_t index) noexcept {
        for (std::size_t i = 0; i < kCount; ++i)
            if (kRegs[i].opcode == opcode && kRegs[i].index == index)
                return slot_at(i);
        return not_a_level();
    }

    [[nodiscard]] static constexpr std::uint32_t stick_word(StickPos pos) noexcept {
        return (static_cast<std::uint32_t>(pos.y) << 8) | pos.x;
    }

    void put(Slot level, std::uint32_t word) noexcept { words_[level] = word; }

    [[nodiscard]] std::optional<std::uint32_t> at(Slot level) const noexcept {
        return words_[level];
    }

    [[nodiscard]] Ex<void> replay(Slot level, ILinkPadEncoder& to) const;

private:
    infra::IndexedArray<std::optional<std::uint32_t>, kCount, Slot> words_{};
};

inline constexpr std::array<PadLevels::Reg, PadLevels::kCount> PadLevels::kRegs = [] {
    std::array<Reg, kCount> r{};
    for (std::uint8_t p = 0; p < kPorts; ++p) {
        r[p] = Reg{JoystickPort::digital_opcode(p), 0};
        r[kPorts + p] = Reg{UioOp::Astick, JoystickPort::stick_index(PlayerIndex{p})};
        r[2 * kPorts + p] = Reg{UioOp::Astick, JoystickPort::paddle_index(PlayerIndex{p})};
        r[3 * kPorts + p] = Reg{UioOp::Astick2, JoystickPort::stick_index(PlayerIndex{p})};
    }
    return r;
}();

}
