// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores {

enum class ScsiOpcode : std::uint8_t {
    TestReady = 0x00,
    RequestSense = 0x03,
    Format = 0x04,
    Read6 = 0x08,
    Write6 = 0x0A,
    Inquiry = 0x12,
    ModeSense = 0x1A,
    ReadCapacity = 0x25,
    Read10 = 0x28,
    Write10 = 0x2A,
};

inline constexpr std::array<ScsiOpcode, 10> kScsiOpcodes{
    ScsiOpcode::TestReady,
    ScsiOpcode::RequestSense,
    ScsiOpcode::Format,
    ScsiOpcode::Read6,
    ScsiOpcode::Write6,
    ScsiOpcode::Inquiry,
    ScsiOpcode::ModeSense,
    ScsiOpcode::ReadCapacity,
    ScsiOpcode::Read10,
    ScsiOpcode::Write10,
};

[[nodiscard]] constexpr std::optional<ScsiOpcode> opcode_of(std::uint8_t raw) noexcept {
    for (const ScsiOpcode op : kScsiOpcodes)
        if (static_cast<std::uint8_t>(op) == raw)
            return op;
    return std::nullopt;
}

enum class ScsiStatus : std::uint8_t { Good = 0x00, Check = 0x02 };

enum class SenseCode : std::uint8_t {
    None = 0x00,
    WriteError = 0x0C,
    ReadError = 0x11,
    InvalidCommand = 0x20,
    LbaOutOfRange = 0x21,
    InvalidLun = 0x25,
    WriteProtected = 0x27,
};

enum class DmaOp : std::uint16_t {
    WriteMemory = 0x08,
    ReadMemory = 0x09,
    Ack = 0x0A,
    Nak = 0x0B,
};

struct Cdb {
    static constexpr std::uint8_t kShortBytes = 6;
    static constexpr std::uint8_t kLongBytes = 10;
    static constexpr unsigned kLunShift = 5;
    static constexpr std::uint32_t kShortLbaMask = 0x1F'FFFF;
    static constexpr std::uint32_t kShortZeroLengthBlocks = 256;
    static constexpr std::size_t kShortLengthAt = 4;
    static constexpr std::size_t kLongLbaAt = 2;
    static constexpr std::size_t kLongLengthAt = 7;
    static constexpr std::size_t kLbaBytes = sizeof(std::uint32_t);
    static constexpr std::size_t kLongLengthBytes = sizeof(std::uint16_t);

    ScsiOpcode opcode = ScsiOpcode::TestReady;
    std::uint8_t lun = 0;
    proto::Lba lba{};
    proto::BlockCount length{};

    [[nodiscard]] constexpr bool writes() const noexcept {
        return opcode == ScsiOpcode::Write6 || opcode == ScsiOpcode::Write10;
    }

    [[nodiscard]] static constexpr std::optional<Cdb> decode(
        std::span<const std::byte> raw
    ) noexcept {
        if (raw.size() < kShortBytes)
            return std::nullopt;
        const auto op = opcode_of(std::to_integer<std::uint8_t>(raw[0]));
        if (!op)
            return std::nullopt;
        Cdb out{};
        out.opcode = *op;
        out.lun = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(raw[1]) >> kLunShift);
        if (*op == ScsiOpcode::Read10 || *op == ScsiOpcode::Write10) {
            if (raw.size() < kLongBytes)
                return std::nullopt;
            out.lba = proto::Lba{
                infra::load_be<std::uint32_t>(raw.subspan<kLongLbaAt, kLbaBytes>())
            };
            out.length = proto::BlockCount{
                infra::load_be<std::uint16_t>(raw.subspan<kLongLengthAt, kLongLengthBytes>())
            };
        } else {
            out.lba = proto::Lba{
                infra::load_be<std::uint32_t>(raw.subspan<0, kLbaBytes>()) & kShortLbaMask
            };
            const std::uint32_t n = std::to_integer<std::uint8_t>(raw[kShortLengthAt]);
            out.length = proto::BlockCount{n == 0 ? kShortZeroLengthBlocks : n};
        }
        return out;
    }

    friend constexpr bool operator==(Cdb, Cdb) = default;
};

static_assert(sizeof(ScsiOpcode) == 1 && sizeof(ScsiStatus) == 1 && sizeof(SenseCode) == 1);
static_assert(sizeof(DmaOp) == 2);

}
