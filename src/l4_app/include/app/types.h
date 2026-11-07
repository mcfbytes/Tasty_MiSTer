// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "app/link_encoder.h"
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

struct RowCount {
    std::uint32_t v = 0;
    using affine_count = RowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr RowCount of(I n) noexcept {
        return RowCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RowCount half() const noexcept { return RowCount{v >> 1u}; }

    [[nodiscard]] constexpr std::int32_t extent(std::int32_t pitch) const noexcept {
        return static_cast<std::int32_t>(v) * pitch;
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

[[nodiscard]] constexpr proto::DipRowIndex dip_row_of(RowIndex row) noexcept {
    return proto::DipRowIndex::of(row.v);
}

[[nodiscard]] constexpr cores::ConfigSlot config_slot_of(RowIndex i) noexcept {
    return cores::ConfigSlot{static_cast<std::uint8_t>(i.v)};
}

static_assert(sizeof(RowCount) == 4);
static_assert(sizeof(RowIndex) == 4);

struct Delivered {
    ILinkEncoder::Outcome outcome = ILinkEncoder::Outcome::Dropped;
    bool round_spent = false;
};

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

struct ControlGeneration {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr ControlGeneration next() const noexcept {
        return ControlGeneration{static_cast<std::uint16_t>(v + 1u)};
    }

    [[nodiscard]] constexpr ControlGeneration next_live() const noexcept {
        const ControlGeneration n = next();
        return n.v == 0 ? ControlGeneration{1} : n;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        ControlGeneration g
    ) noexcept {
        o.field(key, g.v);
    }

    friend constexpr bool operator==(ControlGeneration, ControlGeneration) = default;
};

struct FrameCount {
    std::int32_t v = 0;
    using affine_count = FrameCount;

    [[nodiscard]] constexpr bool multiple_of(FrameCount stride) const noexcept {
        return v % stride.v == 0;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(FrameCount, FrameCount) = default;
    friend constexpr auto operator<=>(FrameCount, FrameCount) = default;
};

struct MovieFrame {
    std::int32_t v = 0;
    using affine_count = FrameCount;

    [[nodiscard]] constexpr std::uint32_t to_wire() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(MovieFrame, MovieFrame) = default;
    friend constexpr auto operator<=>(MovieFrame, MovieFrame) = default;
};

inline constexpr MovieFrame kNoMovieFrame{-1};

[[nodiscard]] constexpr MovieFrame movie_frame_from_wire(std::uint32_t wire) noexcept {
    return MovieFrame{static_cast<std::int32_t>(wire)};
}

struct FrameStamp;

struct CoreFrameCount {
    std::uint32_t v = 0;
    using affine_count = CoreFrameCount;

    [[nodiscard]] constexpr FrameStamp after(const FrameStamp& s) const noexcept;
    [[nodiscard]] constexpr FrameStamp before(const FrameStamp& s) const noexcept;

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

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

    [[nodiscard]] constexpr bool beyond(CoreFrameGap tol) const noexcept {
        return v > tol.v || v < -tol.v;
    }

    [[nodiscard]] constexpr std::int64_t lasting_ns(std::int64_t period_ns) const noexcept {
        return v * period_ns;
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

struct InputRequestBits {
    std::uint32_t v = 0;
    friend constexpr bool operator==(InputRequestBits, InputRequestBits) = default;
};

struct InputGateBits {
    std::uint32_t v = 0;
    friend constexpr bool operator==(InputGateBits, InputGateBits) = default;
};

struct CoreEdgeSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr CoreEdgeSeq next() const noexcept { return CoreEdgeSeq{v + 1u}; }

    friend constexpr bool operator==(CoreEdgeSeq, CoreEdgeSeq) = default;
};

struct VideoModeIndex {
    std::uint8_t v = 0;
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
