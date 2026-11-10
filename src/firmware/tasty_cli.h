// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include "app/path_text.h"
#include "app/rec_options.h"
#include "app/types.h"
#include "cores/movie_codec.h"
#include "infra/error.h"

namespace mister::fw {

enum class TastyVerb : std::uint8_t {
    Help,
    Play,
    Info,
    Check,
    Status,
    Stop,
    RecStart,
    RecStop,
};

enum class TastyRefusal : std::uint32_t {
    NoVerb = 1,
    NoMovie,
    MovieTooLong,
    NoRecAction,
    BadRecAction,
    UnknownVerb,
    BadRom,
    BadCore,
    BadRecord,
    BadLead,
    BadLinger,
    BadVsyncAdjust,
    BadStopAt,
    BadRamInit,
    BadPhase,
    UnknownOption,
    CheckNoRom,
    BadCodec,
    BadScale,
    BadEvery,
    BadFrom,
    BadTo,
    BadSegment,
    BadRecBounds,
    RecOptionsWithoutRecord,
    BadMotion,
    SliceOnRecStart,
    BadSave,
    SaveNotPlay,
    kCount,
};

[[nodiscard]] constexpr std::optional<TastyRefusal> tasty_refusal_of(
    std::uint32_t detail
) noexcept {
    if (detail == 0 || detail >= static_cast<std::uint32_t>(TastyRefusal::kCount)) {
        return std::nullopt;
    }
    return static_cast<TastyRefusal>(detail);
}

[[nodiscard]] constexpr std::uint32_t tasty_refusal_detail(TastyRefusal r) noexcept {
    return static_cast<std::uint32_t>(r);
}

inline constexpr std::chrono::seconds kDefaultLinger{30};

struct TastyArgs {
    TastyVerb verb = TastyVerb::Play;
    app::PathText movie{};
    app::PathText rom{};
    app::PathText core{};

    std::optional<app::PathText> record{};

    std::optional<app::FrameCount> lead{};

    std::optional<std::chrono::microseconds> phase_us{};

    std::optional<app::MovieFrame> stop_at{};
    std::optional<cores::IReplayCore::RamFill> ram_fill{};

    std::optional<app::PathText> save{};

    std::chrono::seconds linger = kDefaultLinger;
    app::VsyncAdjust vsync_adjust = app::VsyncAdjust::Off;
    bool stay = false;
    bool strict = false;
    bool no_splash = false;
    bool loop = false;
    bool hashes_only = false;
    app::RecOptions rec{};
};

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept;
[[nodiscard]] const char* tasty_verb_name(TastyVerb v) noexcept;

[[nodiscard]] const char* tasty_args_refusal(std::uint32_t detail) noexcept;
void tasty_print_usage(std::FILE* out = stderr) noexcept;

}
