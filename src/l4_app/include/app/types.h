// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "app/percent.h"
#include "app/percents.h"
#include "app/play_refusal.h"
#include "app/ratio.h"
#include "cores/types.h"
#include "hal/types.h"
#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/json_out.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

using proto::CorrelationTag;
using proto::kUncaused;

struct RowPitch {
    std::int32_t v = 0;
    friend constexpr bool operator==(RowPitch, RowPitch) = default;
};

struct RowExtent {
    std::int32_t v = 0;
    using affine_count = RowExtent;

    [[nodiscard]] constexpr bool empty() const noexcept { return v <= 0; }

    [[nodiscard]] constexpr std::int32_t share_of(std::int32_t total) const noexcept {
        return v > 0 ? total / v : 0;
    }

    [[nodiscard]] constexpr std::int32_t over(std::int32_t base) const noexcept { return base + v; }

    friend constexpr bool operator==(RowExtent, RowExtent) = default;
};

struct RowCount {
    std::uint32_t v = 0;
    using affine_count = RowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr RowCount of(I n) noexcept {
        return RowCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] static constexpr RowCount of(proto::DipRowCount n) noexcept {
        return RowCount{n.v};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RowCount half() const noexcept { return RowCount{v >> 1u}; }

    [[nodiscard]] constexpr RowExtent extent(RowPitch pitch) const noexcept {
        return RowExtent{static_cast<std::int32_t>(v) * pitch.v};
    }

    template <class Rows>
    constexpr void clip(Rows& rows) const {
        if (rows.size() > v)
            rows.resize(v);
    }

    friend constexpr bool operator==(RowCount, RowCount) = default;
    friend constexpr auto operator<=>(RowCount, RowCount) = default;
};

struct RowIndex {
    std::uint32_t v = 0;
    using affine_count = RowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr RowIndex of(I n) noexcept {
        return RowIndex{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr RowCount before() const noexcept { return RowCount{v}; }

    [[nodiscard]] constexpr RowIndex next() const noexcept { return RowIndex{v + 1u}; }

    friend constexpr bool operator==(RowIndex, RowIndex) = default;
    friend constexpr auto operator<=>(RowIndex, RowIndex) = default;
};

[[nodiscard]] constexpr RowIndex row_after(RowCount count) noexcept {
    return RowIndex{count.v};
}

[[nodiscard]] constexpr RowCount saturating_sub(RowCount count, RowCount height) noexcept {
    return RowCount{count.v > height.v ? count.v - height.v : 0u};
}

[[nodiscard]] constexpr proto::OsdRowCount osd_rows_of(RowCount height) noexcept {
    return proto::OsdRowCount::of(height.v);
}

[[nodiscard]] constexpr proto::OsdRow last_osd_row(RowCount height) noexcept {
    return proto::OsdRow::of(height.v - 1u);
}

[[nodiscard]] constexpr proto::OsdRow osd_row_at(RowCount offset) noexcept {
    return proto::OsdRow::of(offset.v);
}

[[nodiscard]] constexpr RowIndex row_index_of(proto::OsdRow row) noexcept {
    return RowIndex{row.v};
}

struct PixelCount {
    std::uint64_t v = 0;
    using affine_count = PixelCount;

    template <std::integral I>
    [[nodiscard]] static constexpr PixelCount of(I n) noexcept {
        return PixelCount{static_cast<std::uint64_t>(n)};
    }

    [[nodiscard]] constexpr PixelCount quarter() const noexcept {
        constexpr std::uint64_t kQuarterDivisor = 4;
        return PixelCount{v / kQuarterDivisor};
    }

    friend constexpr bool operator==(PixelCount, PixelCount) = default;
    friend constexpr auto operator<=>(PixelCount, PixelCount) = default;
};

struct PixelIndex {
    std::uint32_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr PixelIndex of(I n) noexcept {
        return PixelIndex{static_cast<std::uint32_t>(n)};
    }

    template <class T>
    [[nodiscard]] constexpr std::optional<std::span<T>> subspan_of(
        std::span<T> pixels,
        PixelCount available
    ) const noexcept {
        if (PixelCount::of(v) > available)
            return std::nullopt;
        return pixels.subspan(v);
    }

    template <class T>
    [[nodiscard]] constexpr std::optional<std::span<T>> subspan(
        std::span<T> pixels
    ) const noexcept {
        return subspan_of(pixels, PixelCount::of(pixels.size()));
    }

    friend constexpr bool operator==(PixelIndex, PixelIndex) = default;
    friend constexpr auto operator<=>(PixelIndex, PixelIndex) = default;
};

[[nodiscard]] constexpr proto::DipRowIndex dip_row_of(RowIndex row) noexcept {
    return proto::DipRowIndex::of(row.v);
}

[[nodiscard]] constexpr cores::ConfigSlot config_slot_of(RowIndex i) noexcept {
    return cores::ConfigSlot{static_cast<std::uint8_t>(i.v)};
}

static_assert(sizeof(RowCount) == 4);
static_assert(sizeof(RowIndex) == 4);

enum class CycleDir : std::int8_t { Next, Back };

enum class ContentEdge : std::uint8_t {
    Content = 0,
    Silent = 1,
    ChainEnd = 2,
};

struct Rgb888 {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::array<std::uint8_t, 3> channels() const noexcept {
        constexpr unsigned kRedShift = 16;
        constexpr unsigned kGreenShift = 8;
        return {
            static_cast<std::uint8_t>(v >> kRedShift),
            static_cast<std::uint8_t>(v >> kGreenShift),
            static_cast<std::uint8_t>(v)
        };
    }

    friend constexpr bool operator==(Rgb888, Rgb888) = default;
};

[[nodiscard]] constexpr Rgb888 rgb_from_channels(const std::array<std::uint8_t, 3>& rgb) noexcept {
    constexpr unsigned kRedShift = 16;
    constexpr unsigned kGreenShift = 8;
    const auto& [r, g, b] = rgb;
    return Rgb888{
        (static_cast<std::uint32_t>(r) << kRedShift)
        | (static_cast<std::uint32_t>(g) << kGreenShift)
        | b
    };
}

struct ColorChannel {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr ColorChannel of(I n) noexcept {
        return ColorChannel{static_cast<std::uint8_t>(n)};
    }

    template <std::unsigned_integral W>
    [[nodiscard]] constexpr W scaled_by(W weight) const noexcept {
        return static_cast<W>(v) * weight;
    }

    [[nodiscard]] constexpr Rgb888 rgb_with(ColorChannel g, ColorChannel b) const noexcept {
        return rgb_from_channels({v, g.v, b.v});
    }

    friend constexpr bool operator==(ColorChannel, ColorChannel) = default;
};

[[nodiscard]] constexpr Rgb888 rgb_from_channels(
    ColorChannel r,
    ColorChannel g,
    ColorChannel b
) noexcept {
    return r.rgb_with(g, b);
}

[[nodiscard]] constexpr Rgb888 grey_of(ColorChannel level) noexcept {
    return rgb_from_channels(level, level, level);
}

struct DimQ8 {
    std::uint16_t v = 0;

    static constexpr std::uint16_t kFull = 256;

    [[nodiscard]] static constexpr DimQ8 of(float factor) noexcept {
        constexpr float kRound = 0.5f;
        const float capped = factor < 0.f ? 0.f : (factor > 1.f ? 1.f : factor);
        return DimQ8{static_cast<std::uint16_t>(capped * static_cast<float>(kFull) + kRound)};
    }

    [[nodiscard]] static constexpr DimQ8 of_percents(Percent open, Percent under) noexcept {
        return of_percents(Percents{open, under});
    }

    [[nodiscard]] static constexpr DimQ8 of_percents(Percents p) noexcept {
        return DimQ8{p.open.compose_q8(p.under)};
    }

    [[nodiscard]] constexpr ColorChannel apply(ColorChannel channel) const noexcept {
        return ColorChannel{
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(channel.v) * v / kFull)
        };
    }

    [[nodiscard]] constexpr DimQ8 eased(Ratio ratio) const noexcept {
        return DimQ8{static_cast<std::uint16_t>(ratio.applied_to(v))};
    }

    friend constexpr bool operator==(DimQ8, DimQ8) = default;
};

struct ControlGeneration {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr ControlGeneration next() const noexcept {
        return ControlGeneration{static_cast<std::uint16_t>(v + 1u)};
    }

    [[nodiscard]] constexpr ControlGeneration next_live() const noexcept {
        const ControlGeneration n = next();
        return n.v == 0 ? ControlGeneration{1} : n;
    }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        ControlGeneration g
    ) noexcept {
        o.field(key, g.v);
    }

    friend constexpr bool operator==(ControlGeneration, ControlGeneration) = default;

    [[nodiscard]] constexpr bool ahead_of(ControlGeneration earlier) const noexcept {
        return static_cast<std::int16_t>(v - earlier.v) > 0;
    }
};

struct FrameCount {
    std::int32_t v = 0;
    using affine_count = FrameCount;

    [[nodiscard]] constexpr bool multiple_of(FrameCount stride) const noexcept {
        return v % stride.v == 0;
    }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    [[nodiscard]] constexpr cores::ReplayLead as_lead() const noexcept {
        return cores::ReplayLead{v};
    }

    [[nodiscard]] static constexpr FrameCount of(cores::ReplayLead lead) noexcept {
        return FrameCount{lead.frames()};
    }

    friend constexpr bool operator==(FrameCount, FrameCount) = default;
    friend constexpr auto operator<=>(FrameCount, FrameCount) = default;
};

struct LeadRefusal {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr LeadRefusal of(FrameCount lead) noexcept {
        return LeadRefusal{static_cast<std::uint32_t>(lead.v)};
    }

    [[nodiscard]] static constexpr LeadRefusal of(cores::ReplayLead lead) noexcept {
        return of(FrameCount{lead.frames()});
    }

    [[nodiscard]] constexpr PlayRefusal refused(PlayRefusal::Why why) const noexcept {
        return PlayRefusal{why, v};
    }

    friend constexpr bool operator==(LeadRefusal, LeadRefusal) = default;
};

struct MovieFrame {
    std::int32_t v = 0;
    using affine_count = FrameCount;

    [[nodiscard]] constexpr std::uint32_t to_wire() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr bool operator==(MovieFrame, MovieFrame) = default;
    friend constexpr auto operator<=>(MovieFrame, MovieFrame) = default;
};

inline constexpr MovieFrame kNoMovieFrame{-1};

[[nodiscard]] constexpr MovieFrame movie_frame_from_wire(std::uint32_t wire) noexcept {
    return MovieFrame{static_cast<std::int32_t>(wire)};
}

struct CoreFrameCount {
    std::uint32_t v = 0;
    using affine_count = CoreFrameCount;

    [[nodiscard]] constexpr hal::HeaderCtr::Lag header_lag() const noexcept {
        return hal::HeaderCtr::Lag::of_frames(v);
    }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    [[nodiscard]] constexpr bool odd() const noexcept { return (v & 1u) != 0u; }

    [[nodiscard]] constexpr std::chrono::nanoseconds lasting(
        std::chrono::nanoseconds period
    ) const noexcept {
        return period * v;
    }

    [[nodiscard]] constexpr CoreFrameCount masked(CoreFrameCount mask) const noexcept {
        return CoreFrameCount{v & mask.v};
    }

    [[nodiscard]] static constexpr CoreFrameCount every_bit() noexcept {
        return CoreFrameCount{~std::uint32_t{}};
    }

    friend constexpr bool operator==(CoreFrameCount, CoreFrameCount) = default;
    friend constexpr auto operator<=>(CoreFrameCount, CoreFrameCount) = default;
};

struct MovieSpan {
    std::int64_t v = 0;
    using affine_count = MovieSpan;

    [[nodiscard]] constexpr CoreFrameCount core_frames() const noexcept {
        return CoreFrameCount{static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(MovieSpan, MovieSpan) = default;
    friend constexpr auto operator<=>(MovieSpan, MovieSpan) = default;
};

struct ArmLead {
    std::int16_t v = 0;

    [[nodiscard]] static constexpr std::optional<ArmLead> of(FrameCount lead) noexcept {
        constexpr FrameCount kArmLeadMin{std::numeric_limits<std::int16_t>::min()};
        constexpr FrameCount kArmLeadMax{std::numeric_limits<std::int16_t>::max()};
        if (lead < kArmLeadMin || lead > kArmLeadMax)
            return std::nullopt;
        return ArmLead{static_cast<std::int16_t>(lead.v)};
    }

    [[nodiscard]] static constexpr std::optional<ArmLead> of(cores::ReplayLead lead) noexcept {
        return of(FrameCount{lead.frames()});
    }

    [[nodiscard]] constexpr MovieSpan movie_span() const noexcept { return MovieSpan{v}; }

    friend constexpr bool operator==(ArmLead, ArmLead) = default;
};

struct WideMovieFrame {
    std::int64_t v = 0;
    using affine_count = MovieSpan;

    [[nodiscard]] constexpr MovieFrame narrowed() const noexcept {
        return MovieFrame{static_cast<std::int32_t>(v)};
    }

    friend constexpr bool operator==(WideMovieFrame, WideMovieFrame) = default;

    friend constexpr bool operator<(WideMovieFrame a, WideMovieFrame b) noexcept {
        return a.v < b.v;
    }

    friend constexpr bool operator>(WideMovieFrame a, WideMovieFrame b) noexcept {
        return a.v > b.v;
    }

    friend constexpr bool operator<=(WideMovieFrame a, WideMovieFrame b) noexcept {
        return a.v <= b.v;
    }

    friend constexpr bool operator>=(WideMovieFrame a, WideMovieFrame b) noexcept {
        return a.v >= b.v;
    }
};

inline constexpr WideMovieFrame kNoWideMovieFrame{-1};

[[nodiscard]] constexpr WideMovieFrame wide_frame(MovieFrame f) noexcept {
    return WideMovieFrame{f.v};
}

struct CoreFrameGap {
    std::int64_t v = 0;
    using affine_count = CoreFrameGap;

    [[nodiscard]] static constexpr CoreFrameGap of_lead(hal::HeaderCtr::Lag lead) noexcept {
        return CoreFrameGap{lead.v};
    }

    [[nodiscard]] static constexpr CoreFrameGap of_count(CoreFrameCount n) noexcept {
        return CoreFrameGap{n.v};
    }

    [[nodiscard]] constexpr CoreFrameGap truncated_to(CoreFrameGap unit) const noexcept {
        return CoreFrameGap{unit.v * (v / unit.v)};
    }

    [[nodiscard]] constexpr CoreFrameGap halved() const noexcept { return CoreFrameGap{v / 2}; }

    [[nodiscard]] constexpr bool odd() const noexcept { return (v & 1) != 0; }

    [[nodiscard]] constexpr bool beyond(CoreFrameGap tol) const noexcept {
        return v > tol.v || v < -tol.v;
    }

    [[nodiscard]] constexpr std::chrono::nanoseconds lasting_ns(
        std::chrono::nanoseconds period
    ) const noexcept {
        return std::chrono::nanoseconds{v * period.count()};
    }

    [[nodiscard]] constexpr std::int64_t period_ns_of(std::int64_t span_ns) const noexcept {
        return span_ns / v;
    }

    [[nodiscard]] constexpr CoreFrameCount core_frames() const noexcept {
        return CoreFrameCount{static_cast<std::uint32_t>(v)};
    }

    [[nodiscard]] constexpr MovieSpan movie_span() const noexcept { return MovieSpan{v}; }

    friend constexpr bool operator==(CoreFrameGap, CoreFrameGap) = default;
    friend constexpr auto operator<=>(CoreFrameGap, CoreFrameGap) = default;
};

struct CountParity {
    bool v = false;

    [[nodiscard]] static constexpr CountParity of(CoreFrameCount n) noexcept {
        return CountParity{n.odd()};
    }

    [[nodiscard]] constexpr CountParity after(CoreFrameGap frames) const noexcept {
        return CountParity{v != frames.odd()};
    }

    friend constexpr bool operator==(CountParity, CountParity) = default;
};

struct Wrapping {
    CoreFrameCount raw{};
    CoreFrameCount mask{};

    [[nodiscard]] constexpr CoreFrameCount since(Wrapping earlier) const noexcept {
        return (raw - earlier.raw).masked(mask);
    }

    [[nodiscard]] static constexpr Wrapping of(hal::FrameSeq s) noexcept {
        return {CoreFrameCount{s.word()}, CoreFrameCount::every_bit()};
    }

    [[nodiscard]] constexpr CountParity parity() const noexcept { return CountParity::of(raw); }

    friend constexpr bool operator==(Wrapping, Wrapping) = default;
};

struct WideCoreFrame {
    std::int64_t v = 0;
    using affine_count = CoreFrameGap;

    [[nodiscard]] static constexpr WideCoreFrame numbered(std::uint64_t n) noexcept {
        return WideCoreFrame{static_cast<std::int64_t>(n)};
    }

    [[nodiscard]] constexpr std::uint64_t number() const noexcept {
        return static_cast<std::uint64_t>(v);
    }

    [[nodiscard]] constexpr bool on_every(std::uint32_t every) const noexcept {
        return number() % every == 0;
    }

    friend constexpr bool operator==(WideCoreFrame, WideCoreFrame) = default;
    friend constexpr auto operator<=>(WideCoreFrame, WideCoreFrame) = default;
};

struct FabricGeneration {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FabricGeneration next() const noexcept {
        return FabricGeneration{v + 1u};
    }

    friend constexpr bool operator==(FabricGeneration, FabricGeneration) = default;
};

struct CoreEdgeSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr CoreEdgeSeq next() const noexcept { return CoreEdgeSeq{v + 1u}; }

    friend constexpr bool operator==(CoreEdgeSeq, CoreEdgeSeq) = default;
};

struct ResendSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ResendSeq next() const noexcept { return ResendSeq{v + 1u}; }

    friend constexpr bool operator==(ResendSeq, ResendSeq) = default;
};

struct CaptureToken {
    std::uint32_t v = 0;
    static const CaptureToken kNone;

    [[nodiscard]] constexpr CaptureToken next() const noexcept {
        const CaptureToken n{v + 1u};
        return n == kNone ? CaptureToken{1} : n;
    }

    friend constexpr bool operator==(CaptureToken, CaptureToken) = default;
};

inline constexpr CaptureToken CaptureToken::kNone{0};

struct VideoModeIndex {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr VideoModeIndex from_preset(svc::PresetIndex preset) noexcept {
        return VideoModeIndex{preset.v};
    }

    friend constexpr bool operator==(VideoModeIndex, VideoModeIndex) = default;
};

inline constexpr VideoModeIndex kVideoModeIndexNever{0xFF};
inline constexpr VideoModeIndex kVideoModeIndexCustom{0xFE};

struct HdLayoutIndex {
    std::uint8_t v = 0;

    template <class T, std::size_t N>
    [[nodiscard]] constexpr const T& in(const std::array<T, N>& table) const noexcept {
        return table[v];
    }

    template <class T, std::size_t N>
    [[nodiscard]] constexpr bool within(const std::array<T, N>&) const noexcept {
        return v < N;
    }

    friend constexpr bool operator==(HdLayoutIndex, HdLayoutIndex) = default;
};

struct HdPaletteIndex {
    std::uint8_t v = 0;

    template <class T, std::size_t N>
    [[nodiscard]] constexpr const T& in(const std::array<T, N>& table) const noexcept {
        return table[v];
    }

    template <class T, std::size_t N>
    [[nodiscard]] constexpr bool within(const std::array<T, N>&) const noexcept {
        return v < N;
    }

    friend constexpr bool operator==(HdPaletteIndex, HdPaletteIndex) = default;
};

struct HdColors {
    std::uint8_t v = 0;

    template <class T, std::size_t N>
    [[nodiscard]] constexpr bool within(const std::array<T, N>&) const noexcept {
        return v <= N;
    }

    [[nodiscard]] constexpr bool is_auto() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::optional<HdPaletteIndex> palette() const noexcept {
        if (v == 0)
            return std::nullopt;
        return HdPaletteIndex{static_cast<std::uint8_t>(v - 1u)};
    }

    friend constexpr bool operator==(HdColors, HdColors) = default;
};

struct StockThemeIndex {
    std::uint8_t v = 0;

    template <class T, std::size_t N>
    [[nodiscard]] constexpr const T& pick(const std::array<T, N>& table) const noexcept {
        const std::size_t row = v;
        return table[row < N ? row : 0];
    }

    friend constexpr bool operator==(StockThemeIndex, StockThemeIndex) = default;
};

struct KibCount {
    std::uint32_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr KibCount of(I n) noexcept {
        return KibCount{static_cast<std::uint32_t>(n)};
    }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, KibCount kb) noexcept {
        o.field(key, kb.v);
    }

    friend constexpr bool operator==(KibCount, KibCount) = default;
    friend constexpr auto operator<=>(KibCount, KibCount) = default;
};

struct PixelBytes {
    std::uint16_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr PixelBytes of(I n) noexcept {
        return PixelBytes{static_cast<std::uint16_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t span(std::size_t pixels) const noexcept {
        return pixels * static_cast<std::size_t>(v);
    }

    friend constexpr bool operator==(PixelBytes, PixelBytes) = default;
};

struct RowAlign {
    std::uint32_t v = 1;

    template <std::integral I>
    [[nodiscard]] static constexpr RowAlign of(I n) noexcept {
        return RowAlign{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t round_up(std::size_t bytes) const noexcept {
        return (bytes + v - 1u) & ~static_cast<std::size_t>(v - 1u);
    }

    friend constexpr bool operator==(RowAlign, RowAlign) = default;
};

struct PreludeCompletions {
    std::uint32_t v = 0;
    friend constexpr bool operator==(PreludeCompletions, PreludeCompletions) = default;
};

struct FrameWordCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::span<const std::uint16_t> prefix_of(
        std::span<const std::uint16_t> all
    ) const noexcept {
        return all.first(v);
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool exceeds(FrameWordCount bound) const noexcept {
        return v > bound.v;
    }

    friend constexpr bool operator==(FrameWordCount, FrameWordCount) = default;
};

enum class VsyncAdjust : std::uint8_t { Off, Match, LowLag };

using svc::DirectVideo;

enum class VgaMode : std::uint8_t { Rgb, YPbPr, SVideo, Cvbs, Subcarrier };

[[nodiscard]] constexpr std::uint8_t wire_of(VgaMode m) noexcept {
    return static_cast<std::uint8_t>(m);
}

[[nodiscard]] constexpr std::optional<VgaMode> vga_mode_of(std::uint8_t raw) noexcept {
    if (raw > wire_of(VgaMode::Subcarrier))
        return std::nullopt;
    return static_cast<VgaMode>(raw);
}

}
