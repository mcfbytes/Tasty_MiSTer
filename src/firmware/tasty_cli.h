// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

struct TastyRefusal {
    enum Code : std::uint32_t {
        NoVerb = 1,
        NoMovie,
        MovieTooLong,
        NoRecAction,
        BadRecAction,
        UnknownVerb,
        BadRom = 10,
        BadCore,
        BadRecord,
        BadLead,
        BadLinger,
        BadVsyncAdjust,
        BadStopAt,
        BadRamInit,
        BadPhase,
        UnknownOption = 20,
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
    };
};

struct TastyArgs {
    TastyVerb verb = TastyVerb::Play;
    app::PathText movie{};
    app::PathText rom{};
    app::PathText core{};

    std::optional<app::PathText> record{};
    std::optional<std::int32_t> lead{};
    std::optional<std::uint32_t> phase_us{};
    std::optional<std::uint32_t> stop_at{};
    std::optional<cores::IMovieCodec::RamFill> ram_fill{};

    std::optional<app::PathText> save{};
    std::uint32_t return_after_s = 30;
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
