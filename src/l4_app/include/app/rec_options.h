// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "app/row_bytes.h"
#include "app/types.h"
#include "infra/endian.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "app/rec_every.h"
#include "proto/types.h"

namespace mister::app {

enum class RecCodec : std::uint8_t { Cscd, Zmbv, kCount };

inline constexpr bool kCscdBuildAvailable = true;

struct BitsPerPixel {
    std::uint16_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr BitsPerPixel of(I n) noexcept {
        return BitsPerPixel{static_cast<std::uint16_t>(n)};
    }

    constexpr void store_le16(std::span<std::byte, sizeof(std::uint16_t)> dst) const noexcept {
        infra::store_le<std::uint16_t>(dst, v);
    }

    friend constexpr bool operator==(BitsPerPixel, BitsPerPixel) = default;
};

struct RecCodecFacts {
    RecCodec kind;
    std::string_view name;
    char fourcc[5];
    BitsPerPixel bits_per_pixel;
    PixelBytes bytes_per_pixel;
    RowAlign row_align;
    bool available;
};

inline constexpr auto kRecCodecFacts = std::to_array<RecCodecFacts>({
    {RecCodec::Cscd,
     "cscd",
     "CSCD",
     BitsPerPixel::of(24),
     PixelBytes::of(3),
     RowAlign::of(4),
     kCscdBuildAvailable},
    {RecCodec::Zmbv,
     "zmbv",
     "ZMBV",
     BitsPerPixel::of(32),
     PixelBytes::of(4),
     RowAlign::of(4),
     true},
});
static_assert(
    infra::rows_are_ordinal(kRecCodecFacts),
    "kRecCodecFacts: exactly one row per RecCodec, in enumerator order"
);

[[nodiscard]] constexpr const RecCodecFacts& rec_codec_facts(RecCodec c) noexcept {
    return kRecCodecFacts[infra::ordinal(c)];
}

enum class RecScale : std::uint8_t { Auto, Native, Half };

enum class PictureScale : std::uint8_t { Native = 1, Half = 2 };

[[nodiscard]] constexpr proto::FieldValue picture_scale_digit(
    std::optional<PictureScale> s
) noexcept {
    return proto::FieldValue{s ? static_cast<std::uint32_t>(*s) : 0u};
}

[[nodiscard]] constexpr std::optional<PictureScale> picture_scale_of(
    proto::FieldValue digit
) noexcept {
    if (digit == picture_scale_digit(PictureScale::Native))
        return PictureScale::Native;
    if (digit == picture_scale_digit(PictureScale::Half))
        return PictureScale::Half;
    return std::nullopt;
}

enum class RecMotion : std::uint8_t { Auto, Off, Small, Full };

struct RecOptions {
    RecCodec codec = RecCodec::Cscd;
    RecScale scale = RecScale::Auto;
    RecMotion motion = RecMotion::Auto;

    std::array<std::byte, 1> pad_{};
    RecEvery every{};
    std::array<std::byte, 2> pad2_{};
    MovieFrame from_frame = kNoMovieFrame;
    MovieFrame to_frame = kNoMovieFrame;
    proto::FileSize segment_bytes{};

    [[nodiscard]] friend bool operator==(const RecOptions&, const RecOptions&) = default;
};

static_assert(static_cast<std::uint8_t>(RecCodec::Cscd) == 0);
static_assert(static_cast<std::uint8_t>(RecScale::Auto) == 0);
static_assert(static_cast<std::uint8_t>(RecMotion::Auto) == 0);
static_assert(std::is_trivially_copyable_v<RecOptions>);
static_assert(std::is_nothrow_default_constructible_v<RecOptions>);
static_assert(sizeof(RecOptions) == 24);

inline constexpr proto::FileSize kRecSegmentMin{16ull << 20};
inline constexpr proto::FileSize kRecSegmentMax{2ull << 30};
static_assert(kRecSegmentMax == proto::FileSize{1ull << 31});

[[nodiscard]] inline bool rec_segment_ok(proto::FileSize bytes) noexcept {
    return bytes.empty() || (bytes >= kRecSegmentMin && bytes <= kRecSegmentMax);
}

[[nodiscard]] inline bool rec_bounds_ok(const RecOptions& o) noexcept {
    if (o.from_frame < kNoMovieFrame || o.to_frame < kNoMovieFrame)
        return false;
    return o.from_frame < MovieFrame{} || o.to_frame < MovieFrame{} || o.to_frame > o.from_frame;
}

[[nodiscard]] const char* rec_codec_name(RecCodec c) noexcept;

[[nodiscard]] bool rec_codec_available(RecCodec c) noexcept;
[[nodiscard]] const char* rec_scale_name(RecScale s) noexcept;
[[nodiscard]] const char* rec_motion_name(RecMotion m) noexcept;
[[nodiscard]] std::optional<RecCodec> parse_rec_codec(std::string_view s) noexcept;
[[nodiscard]] std::optional<RecScale> parse_rec_scale(std::string_view s) noexcept;
[[nodiscard]] std::optional<RecMotion> parse_rec_motion(std::string_view s) noexcept;

[[nodiscard]] std::optional<RecEvery> parse_rec_every(std::string_view s) noexcept;

[[nodiscard]] std::optional<proto::FileSize> parse_rec_size(std::string_view s) noexcept;

[[nodiscard]] FixedStr<160, StrFit::Reject> format_rec_options(const RecOptions& o) noexcept;

}
