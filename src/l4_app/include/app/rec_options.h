// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "infra/fixed_str.h"

namespace mister::app {

enum class RecCodec : std::uint8_t { Cscd, Zmbv };
enum class RecScale : std::uint8_t { Auto, Native, Half };

enum class RecMotion : std::uint8_t { Auto, Off, Small, Full };

inline constexpr std::uint16_t kRecEveryMax = 600;

struct RecOptions {
    RecCodec codec = RecCodec::Cscd;
    RecScale scale = RecScale::Auto;
    RecMotion motion = RecMotion::Auto;
    std::uint16_t every = 1;
    std::int32_t from_frame = -1;
    std::int32_t to_frame = -1;
    std::uint64_t segment_bytes = 0;

    [[nodiscard]] friend bool operator==(const RecOptions&, const RecOptions&) = default;
};

static_assert(static_cast<std::uint8_t>(RecCodec::Cscd) == 0);
static_assert(static_cast<std::uint8_t>(RecScale::Auto) == 0);
static_assert(static_cast<std::uint8_t>(RecMotion::Auto) == 0);
static_assert(std::is_trivially_copyable_v<RecOptions>);
static_assert(std::is_nothrow_default_constructible_v<RecOptions>);
static_assert(sizeof(RecOptions) == 24);

inline constexpr std::uint64_t kRecSegmentMin = 16ull << 20;
inline constexpr std::uint64_t kRecSegmentMax = 2ull << 30;
static_assert(kRecSegmentMax == (1ull << 31));

[[nodiscard]] inline bool rec_segment_ok(std::uint64_t bytes) noexcept {
    return bytes == 0 || (bytes >= kRecSegmentMin && bytes <= kRecSegmentMax);
}

[[nodiscard]] inline bool rec_bounds_ok(const RecOptions& o) noexcept {
    if (o.from_frame < -1 || o.to_frame < -1) return false;
    return o.from_frame < 0 || o.to_frame < 0 || o.to_frame > o.from_frame;
}

[[nodiscard]] const char* rec_codec_name(RecCodec c) noexcept;
[[nodiscard]] const char* rec_scale_name(RecScale s) noexcept;
[[nodiscard]] const char* rec_motion_name(RecMotion m) noexcept;
[[nodiscard]] std::optional<RecCodec> parse_rec_codec(std::string_view s) noexcept;
[[nodiscard]] std::optional<RecScale> parse_rec_scale(std::string_view s) noexcept;
[[nodiscard]] std::optional<RecMotion> parse_rec_motion(std::string_view s) noexcept;

[[nodiscard]] std::optional<std::uint16_t> parse_rec_every(std::string_view s) noexcept;

[[nodiscard]] std::optional<std::uint64_t> parse_rec_size(std::string_view s) noexcept;

[[nodiscard]] FixedStr<160, StrFit::Reject> format_rec_options(const RecOptions& o) noexcept;

}  // namespace mister::app
