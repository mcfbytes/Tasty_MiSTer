// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_types.h"
#include "hal/types.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::atari8::wire {

enum class SioPort : std::uint8_t {
    TxStatus = 0x03,
    Rx = 0x04,
    RxStatus = 0x05,
    Divisor = 0x06,
    Error = 0x07,
};

[[nodiscard]] constexpr hal::SpiWord port_word(SioPort p) noexcept {
    return hal::SpiWord{std::to_underlying(p)};
}

[[nodiscard]] constexpr proto::MailboxRunCommand read(SioPort p) noexcept {
    return {.opcode = port_word(p), .word = hal::SpiWord{}};
}

[[nodiscard]] constexpr proto::MailboxRunCommand transmit(std::byte byte) noexcept {
    return detail::set_byte(WriteRegister::SioTx, byte);
}

[[nodiscard]] constexpr proto::MailboxRunCommand divisor(SioDivisor d) noexcept {
    return detail::set_byte(WriteRegister::SioDivisor, d.wire_byte());
}

inline constexpr std::byte kAck{'A'};
inline constexpr std::byte kNak{'N'};
inline constexpr std::byte kComplete{'C'};
inline constexpr std::byte kError{'E'};

inline constexpr proto::ReplyMask kTxRoom{0x01FF};

inline constexpr proto::ReplyMask kRxHeld{0x02FF};

inline constexpr proto::user_io::PollFrameShape kTxRoomGuard{
    {.opcode = port_word(SioPort::TxStatus),
     .kind = proto::user_io::TransferKind::AckedWord,
     .leading = proto::ReplyCount{1},
     .guard = {
         .condition = proto::user_io::Condition::NonZero,
         .reply = proto::ReplyIndex{},
         .mask = kTxRoom
     }}
};

inline constexpr proto::user_io::PollFrameShape kRxFrame{
    {.opcode = port_word(SioPort::RxStatus),
     .kind = proto::user_io::TransferKind::AckedWord,
     .leading = proto::ReplyCount{1},
     .guard = {
         .condition = proto::user_io::Condition::NonZero,
         .reply = proto::ReplyIndex{},
         .mask = kRxHeld
     }}
};

}
