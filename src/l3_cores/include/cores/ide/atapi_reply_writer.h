// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

class AtapiReplyWriter {
public:
    static constexpr infra::ByteCount kLengthBytes{sizeof(std::uint16_t)};

    explicit constexpr AtapiReplyWriter(std::span<std::byte> out) noexcept : out_(out) {}

    constexpr void put(std::byte b) noexcept {
        if (const auto cell = infra::IndexedSpan<std::byte, infra::ByteOffset>{out_}.find(at_))
            *cell = b;
        at_ += infra::ByteCount{1};
    }

    constexpr void put_zeros(infra::ByteCount count) noexcept {
        for (infra::ByteCount done{}; done < count; done += infra::ByteCount{1})
            put(std::byte{0});
    }

    constexpr void put(std::span<const std::byte> bytes) noexcept {
        for (const std::byte b : bytes)
            put(b);
    }

    constexpr void put(svc::TrackNumber number) noexcept { put_stored(number); }

    constexpr void put(svc::TrackCount count) noexcept { put_stored(count); }

    constexpr void put(svc::TrackIndex index) noexcept { put_stored(index); }

    constexpr void put_address(svc::Msf at, bool as_msf) noexcept {
        std::array<std::byte, sizeof(std::uint32_t)>
            field{std::byte{0}, std::byte{at.m}, std::byte{at.s}, std::byte{at.f}};
        if (!as_msf)
            block_of(at).store_be32(std::span{field});
        put(std::span<const std::byte>{field});
    }

    constexpr void patch_length(infra::ByteOffset at) noexcept {
        const infra::ByteOffset behind = at + kLengthBytes;
        const std::span<std::byte> field = infra::affine::from_clamped(out_, at);
        if (infra::ByteCount{field.size()} < kLengthBytes || at_ < behind)
            return;
        const proto::FileSize length = proto::FileSize::of(at_ - behind);
        infra::store_be<std::uint16_t>(
            field.first<sizeof(std::uint16_t)>(),
            length.file_word<std::uint16_t>()
        );
    }

    [[nodiscard]] constexpr infra::ByteCount written() const noexcept {
        return at_ - infra::ByteOffset{};
    }

    [[nodiscard]] static constexpr svc::Msf msf_of(proto::BlockCount frames) noexcept {
        const std::uint32_t seconds = frames / proto::BlockCount{svc::kFramesPerSecond};
        const auto byte_of = [](proto::BlockCount at) {
            return std::to_integer<std::uint8_t>(at.low_byte());
        };
        return svc::Msf{
            byte_of(proto::BlockCount{seconds / svc::kSecondsPerMinute}),
            byte_of(proto::BlockCount{seconds % svc::kSecondsPerMinute}),
            byte_of(frames - proto::BlockCount{seconds * svc::kFramesPerSecond})
        };
    }

    [[nodiscard]] static constexpr svc::Msf msf_of(proto::Lba block) noexcept {
        return msf_of((block + svc::disc::kRedBookPregap) - proto::Lba{});
    }

    [[nodiscard]] static constexpr proto::Lba block_of(svc::Msf at) noexcept {
        return proto::Lba{at.m * svc::kFramesPerMinute + at.s * svc::kFramesPerSecond + at.f}
        - svc::disc::kRedBookPregap;
    }

private:
    template <class Value>
    constexpr void put_stored(const Value& value) noexcept {
        std::array<std::byte, 1> cell{};
        value.store_byte(std::span<std::byte, 1>{cell});
        put(cell[0]);
    }

    std::span<std::byte> out_;
    infra::ByteOffset at_{};
};

static_assert(AtapiReplyWriter::msf_of(proto::Lba{0}) == svc::Msf{0, 2, 0});
static_assert(AtapiReplyWriter::block_of(svc::Msf{0, 2, 0}) == proto::Lba{0});

}
