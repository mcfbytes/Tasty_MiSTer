// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "app/link_encoder.h"
#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/json_out.h"
#include "proto/types.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

using proto::CorrelationTag;
using proto::kUncaused;

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

struct FabricGeneration {
    std::uint32_t v = 0;
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

enum class DirectVideo : std::uint8_t { Off, On, Auto };

enum class VgaMode : std::uint8_t { Rgb, YPbPr, SVideo, Cvbs };

}
