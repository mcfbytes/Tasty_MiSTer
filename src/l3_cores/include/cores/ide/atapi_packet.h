// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/atapi_packet_op.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/indexed_span.h"
#include "infra/span_cut.h"
#include "proto/types.h"
#include "svc/disc/track.h"
#include "svc/msf.h"
#include "svc/types.h"

namespace mister::cores::ide {

enum class AtapiModePage : std::uint8_t {
    ErrorRecovery = 0x01,
    AudioControl = 0x0E,
    Capabilities = 0x2A,
    All = 0x3F,
};

enum class AtapiTocFormat : std::uint8_t { Toc = 0, Session = 1 };

[[nodiscard]] constexpr std::byte wire_byte(AtapiModePage page) noexcept {
    return static_cast<std::byte>(page);
}

[[nodiscard]] constexpr std::byte wire_byte(AtapiTocFormat format) noexcept {
    return static_cast<std::byte>(format);
}

struct AtapiPacket {
    static constexpr infra::ByteCount kBytes{12};

    struct PageSelect {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool selects(AtapiModePage page) const noexcept {
            return page_byte() == wire_byte(page) || page_byte() == wire_byte(AtapiModePage::All);
        }

        [[nodiscard]] constexpr std::byte page_byte() const noexcept { return std::byte{v}; }

        friend constexpr bool operator==(PageSelect, PageSelect) = default;
    };

    struct MsfAddress {

        static constexpr svc::Msf kCurrent{0xFF, 0xFF, 0xFF};
        svc::Msf at{};

        [[nodiscard]] constexpr bool current() const noexcept { return at == kCurrent; }

        [[nodiscard]] constexpr proto::Lba lba() const noexcept {
            const proto::BlockCount frames{
                at.m * svc::kFramesPerMinute + at.s * svc::kFramesPerSecond + at.f
            };
            if (frames < svc::disc::kRedBookPregap)
                return proto::Lba{};
            return proto::Lba{} + (frames - svc::disc::kRedBookPregap);
        }
    };

    std::array<std::byte, kBytes.size()> raw{};

    [[nodiscard]] static constexpr AtapiPacket of(
        std::span<const std::byte, kBytes.size()> bytes
    ) noexcept {
        AtapiPacket out{};
        std::ranges::copy(bytes, out.raw.begin());
        return out;
    }

    [[nodiscard]] constexpr std::optional<AtapiPacketOp> op() const noexcept {
        return atapi_packet_op_of(at_(kOpAt));
    }

    [[nodiscard]] constexpr proto::Lba lba() const noexcept {
        return proto::Lba{be_at_<std::uint32_t>(kLbaAt)};
    }

    [[nodiscard]] constexpr proto::BlockCount blocks10() const noexcept {
        return proto::BlockCount{be_at_<std::uint16_t>(kField16At)};
    }

    [[nodiscard]] constexpr proto::BlockCount blocks12() const noexcept {
        return proto::BlockCount{be_at_<std::uint32_t>(kBlocks12At)};
    }

    [[nodiscard]] constexpr infra::ByteCount alloc() const noexcept {
        return infra::ByteCount{be_at_<std::uint16_t>(kField16At)};
    }

    [[nodiscard]] constexpr infra::ByteCount short_alloc() const noexcept {
        return infra::ByteCount{std::to_integer<std::uint8_t>(at_(kShortAllocAt))};
    }

    [[nodiscard]] constexpr bool msf() const noexcept { return bit_(kFlagsAt, kMsfBit); }

    [[nodiscard]] constexpr std::optional<AtapiTocFormat> toc_format() const noexcept {
        const std::byte format = at_(kFormatAt) & kTocFormatMask;
        if (format == wire_byte(AtapiTocFormat::Toc))
            return AtapiTocFormat::Toc;
        if (format == wire_byte(AtapiTocFormat::Session))
            return AtapiTocFormat::Session;
        return std::nullopt;
    }

    [[nodiscard]] constexpr svc::TrackNumber toc_start() const noexcept {
        return svc::TrackNumber{std::to_integer<std::uint8_t>(at_(kTocStartAt))};
    }

    [[nodiscard]] constexpr bool sub_q() const noexcept { return bit_(kFormatAt, kSubQBit); }

    [[nodiscard]] constexpr bool sub_position() const noexcept {
        return at_(kSubFormatAt) == kSubPosition;
    }

    [[nodiscard]] constexpr PageSelect page() const noexcept {
        return PageSelect{std::to_integer<std::uint8_t>(at_(kFormatAt))};
    }

    [[nodiscard]] constexpr bool from_first_feature() const noexcept {
        return at_(kFormatAt) == std::byte{0} && at_(kSubFormatAt) == std::byte{0};
    }

    [[nodiscard]] constexpr bool standard_disc_info() const noexcept {
        return (at_(kFlagsAt) & kInfoTypeMask) == std::byte{0};
    }

    [[nodiscard]] constexpr bool info_by_track() const noexcept {
        return at_(kFlagsAt) == kInfoByTrack;
    }

    [[nodiscard]] constexpr svc::TrackIndex info_row() const noexcept {
        return svc::TrackIndex{std::to_integer<std::uint8_t>(at_(kInfoRowAt))};
    }

    [[nodiscard]] constexpr MsfAddress play_from() const noexcept { return msf_at_(kPlayFromAt); }

    [[nodiscard]] constexpr MsfAddress play_to() const noexcept { return msf_at_(kPlayToAt); }

    [[nodiscard]] constexpr bool resume() const noexcept { return bit_(kResumeAt, kResumeBit); }

    friend constexpr bool operator==(const AtapiPacket&, const AtapiPacket&) = default;

private:
    static constexpr infra::ByteOffset kOpAt{0};
    static constexpr infra::ByteOffset kFlagsAt{1};
    static constexpr infra::ByteOffset kFormatAt{2};
    static constexpr infra::ByteOffset kLbaAt{2};
    static constexpr infra::ByteOffset kSubFormatAt{3};
    static constexpr infra::ByteOffset kPlayFromAt{3};
    static constexpr infra::ByteOffset kShortAllocAt{4};
    static constexpr infra::ByteOffset kInfoRowAt{5};
    static constexpr infra::ByteOffset kTocStartAt{6};
    static constexpr infra::ByteOffset kBlocks12At{6};
    static constexpr infra::ByteOffset kPlayToAt{6};
    static constexpr infra::ByteOffset kField16At{7};
    static constexpr infra::ByteOffset kResumeAt{8};

    static constexpr infra::ByteCount kSecondIn{1};
    static constexpr infra::ByteCount kFrameIn{2};
    static constexpr std::byte kMsfBit{0x02};
    static constexpr std::byte kSubQBit{0x40};
    static constexpr std::byte kResumeBit{0x01};
    static constexpr std::byte kTocFormatMask{0x0F};
    static constexpr std::byte kInfoTypeMask{0x07};
    static constexpr std::byte kInfoByTrack{0x01};
    static constexpr std::byte kSubPosition{0x01};

    [[nodiscard]] constexpr std::byte at_(infra::ByteOffset at) const noexcept {
        return infra::IndexedSpan<const std::byte, infra::ByteOffset>{raw}[at];
    }

    template <std::unsigned_integral T>
    [[nodiscard]] constexpr T be_at_(infra::ByteOffset at) const noexcept {
        return infra::load_be<T>(
            infra::affine::from(std::span<const std::byte>{raw}, at).template first<sizeof(T)>()
        );
    }

    [[nodiscard]] constexpr bool bit_(infra::ByteOffset at, std::byte mask) const noexcept {
        return (at_(at) & mask) != std::byte{0};
    }

    [[nodiscard]] constexpr MsfAddress msf_at_(infra::ByteOffset at) const noexcept {
        return MsfAddress{svc::Msf{
            std::to_integer<std::uint8_t>(at_(at)),
            std::to_integer<std::uint8_t>(at_(at + kSecondIn)),
            std::to_integer<std::uint8_t>(at_(at + kFrameIn))
        }};
    }
};

static_assert(
    AtapiPacket::kBytes == infra::ByteCount{sizeof(AtapiPacket)},
    "the packet is its twelve bytes"
);
static_assert(
    AtapiPacket::MsfAddress{svc::Msf{0, 2, 0}}.lba() == proto::Lba{0}
        && AtapiPacket::MsfAddress{svc::Msf{0, 1, 0}}.lba() == proto::Lba{0},
    "00:02:00 is block 0, and an address inside the lead-in floors there"
);

}
