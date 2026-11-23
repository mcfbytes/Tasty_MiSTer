// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "cores/atari8/cart_mode.h"
#include "hal/spi_halves.h"
#include "hal/types.h"
#include "infra/message_sum.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"

namespace mister::cores::atari8::wire {

enum class Opcode : std::uint8_t { GetRegister = 0x08, SetRegister = 0x09, TapeEnqueue = 0x0A };

enum class WriteRegister : std::uint8_t {
    Cart1Select = 0x01,
    Cart2Select = 0x02,
    Reset = 0x03,
    Pause = 0x04,
    Freezer = 0x05,
    ResetRnmi = 0x06,
    XexLoader = 0x09,
    TapeReset = 0x0C,
    OptionForce = 0x07,
    DriveLed = 0x08,
    SioTx = 0x0A,
    SioDivisor = 0x0B,
    StartForce = 0x0D,
    SpaceForce = 0x0E,
};

enum class ReadRegister : std::uint8_t {
    Status1 = 0x01,
    Status3 = 0x03,
    Status2 = 0x02,
    Flash = 0x04
};

enum class Level : std::uint8_t { Low, High };

enum class Pause : std::uint8_t { Run = 0x00, Hold = 0x01, Halted = 0x04 };

[[nodiscard]] constexpr hal::SpiWord word(Opcode op) noexcept {
    return hal::SpiWord{std::to_underlying(op)};
}

namespace detail {
[[nodiscard]] constexpr proto::MailboxRunCommand set_byte(
    WriteRegister r,
    std::uint8_t value
) noexcept {
    return {
        .opcode = word(Opcode::SetRegister),
        .word = hal::SpiWord{value}.with_high_byte(std::to_underlying(r))
    };
}

[[nodiscard]] constexpr proto::MailboxRunCommand set_byte(
    WriteRegister r,
    std::byte value
) noexcept {
    return set_byte(r, std::to_integer<std::uint8_t>(value));
}
}

[[nodiscard]] constexpr proto::MailboxRunCommand set(WriteRegister r, Level level) noexcept {
    return detail::set_byte(r, std::to_underlying(level));
}

[[nodiscard]] constexpr proto::MailboxRunCommand set_cart(WriteRegister r, CartMode mode) noexcept {
    return {
        .opcode = word(Opcode::SetRegister),
        .word = mode.value_word().with_high_byte(std::to_underlying(r))
    };
}

[[nodiscard]] constexpr proto::MailboxRunCommand pause(Pause level) noexcept {
    return detail::set_byte(WriteRegister::Pause, std::to_underlying(level));
}

[[nodiscard]] constexpr proto::MailboxRunCommand enqueue(hal::SpiHalves entry) noexcept {
    return {.opcode = word(Opcode::TapeEnqueue), .word = entry.low, .tail = entry.high};
}

[[nodiscard]] constexpr hal::SpiWord select_word(ReadRegister r) noexcept {
    return hal::SpiWord{}.with_high_byte(std::to_underlying(r));
}

[[nodiscard]] constexpr proto::MailboxRunCommand get(ReadRegister r) noexcept {
    return {.opcode = word(Opcode::GetRegister), .word = select_word(r)};
}

[[nodiscard]] constexpr proto::MailboxAct run_act(const proto::MailboxRun& run) noexcept {
    return infra::make<proto::MailboxAct>(
        proto::MailboxAct::CommandRun{.commands = run.commands()}
    );
}

inline constexpr proto::WideIoIndex kDmaIndex{99};

}
