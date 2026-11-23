// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "cores/minimig/akiko_opcode.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"
#include "svc/disc/q_channel.h"

namespace mister::cores::minimig::akiko {

struct SubqReply {
    std::array<std::byte, 15> v{};

    static constexpr std::byte kNoPosition{0x80};

    static constexpr std::byte kTocEntryMark{0x0A};

    [[nodiscard]] static constexpr SubqReply of(CommandByte head, svc::disc::QChannel q) noexcept {
        SubqReply out;
        out.put(kHeadAt, head.wire_byte());
        out.put_channel(q);
        return out;
    }

    [[nodiscard]] static constexpr SubqReply unknown(CommandByte head) noexcept {
        SubqReply out;
        out.put(kHeadAt, head.wire_byte());
        out.put(kTnoAt, kNoPosition);
        return out;
    }

    [[nodiscard]] static constexpr SubqReply drip(svc::disc::QChannel q) noexcept {
        SubqReply out;
        out.put(kHeadAt, head_byte(Notify::TocEntry));
        out.put(kMarkAt, kTocEntryMark);
        out.put_channel(q);
        return out;
    }

    [[nodiscard]] constexpr std::span<const std::byte, 15> bytes() const noexcept {
        return std::span{v}.first<15>();
    }

private:
    static constexpr infra::ByteOffset kHeadAt{0};
    static constexpr infra::ByteOffset kMarkAt{1};
    static constexpr infra::ByteOffset kQAt{3};
    static constexpr infra::ByteOffset kTnoAt{4};

    constexpr void put(infra::ByteOffset at, std::byte byte) noexcept {
        infra::affine::from(std::span{v}, at).front() = byte;
    }

    constexpr void put_channel(svc::disc::QChannel q) noexcept {
        q.store(infra::affine::from(std::span{v}, kQAt).first<svc::disc::QChannel::kBytes>());
    }
};

}
