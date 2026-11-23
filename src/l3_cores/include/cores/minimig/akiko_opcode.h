// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/reg_field.h"
#include "infra/byte_count.h"
#include "proto/types.h"

namespace mister::cores::minimig::akiko {

enum class Opcode : std::uint8_t {
    Nop,
    Stop,
    Pause,
    Unpause,
    Multi,
    Led,
    Subq,
    Info,
    Silent8,
    Silent9,
    SilentA,
    BadB,
    BadC,
    BadD,
    BadE,
    BadF,
};

inline constexpr Opcode kErrorReply = Opcode::Led;

enum class Notify : std::uint8_t {
    PlayEnd = 0x04,
    TocEntry = 0x06,
    MediaStatus = 0x0A,
};

[[nodiscard]] constexpr std::byte head_byte(Notify head) noexcept {
    return std::byte{static_cast<std::uint8_t>(head)};
}

inline constexpr std::byte kMediaPresent{0x01};
inline constexpr std::byte kMediaAbsent{0x00};

inline constexpr std::array<Opcode, 16> kOpcodes{
    Opcode::Nop,
    Opcode::Stop,
    Opcode::Pause,
    Opcode::Unpause,
    Opcode::Multi,
    Opcode::Led,
    Opcode::Subq,
    Opcode::Info,
    Opcode::Silent8,
    Opcode::Silent9,
    Opcode::SilentA,
    Opcode::BadB,
    Opcode::BadC,
    Opcode::BadD,
    Opcode::BadE,
    Opcode::BadF,
};

inline constexpr std::array<std::optional<proto::ReplyCount>, 16> kPayloads{{
    proto::ReplyCount{1},
    proto::ReplyCount{2},
    proto::ReplyCount{1},
    proto::ReplyCount{1},
    proto::ReplyCount{12},
    proto::ReplyCount{2},
    proto::ReplyCount{1},
    proto::ReplyCount{1},
    proto::ReplyCount{4},
    proto::ReplyCount{1},
    proto::ReplyCount{2},
    std::nullopt,
    std::nullopt,
    std::nullopt,
    std::nullopt,
    std::nullopt,
}};

[[nodiscard]] constexpr std::optional<proto::ReplyCount> payload_of(Opcode opcode) noexcept {
    return kPayloads[std::to_underlying(opcode)];
}

inline constexpr infra::ByteCount kOpcodeByte{1};

[[nodiscard]] constexpr std::optional<infra::ByteCount> frame_bytes(Opcode opcode) noexcept {
    const std::optional<proto::ReplyCount> payload = payload_of(opcode);
    if (!payload)
        return std::nullopt;
    return kOpcodeByte + infra::ByteCount{payload->size()};
}

struct CommandByte {
    std::uint8_t v = 0;

    static constexpr hal::RegField<std::uint8_t> kOpcodeField{0, 0xF};
    static constexpr hal::RegField<std::uint8_t> kSequenceField{4, 0xF};

    [[nodiscard]] constexpr Opcode opcode() const noexcept {
        return static_cast<Opcode>(kOpcodeField.get(v));
    }

    [[nodiscard]] constexpr CommandByte error_echo() const noexcept {
        return CommandByte{kOpcodeField.set(v, {std::to_underlying(kErrorReply)})};
    }

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(CommandByte, CommandByte) = default;
};

}
