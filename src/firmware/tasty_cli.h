// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include "app/path_text.h"
#include "app/capture/rec_options.h"
#include "app/types.h"
#include "cores/tas/movie_codec.h"
#include "infra/error.h"
#include "infra/prelude.h"

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

inline constexpr std::chrono::seconds kDefaultLinger{30};

struct TastyArgs {
    TastyVerb verb = TastyVerb::Play;
    app::PathText movie{};
    app::PathText rom{};
    app::PathText core{};

    std::optional<app::PathText> record{};

    std::optional<app::MovieFrameCount> lead{};

    std::optional<std::chrono::microseconds> phase_us{};

    std::optional<app::MoviePosition> stop_at{};
    std::optional<cores::tas::IReplayCore::RamFill> ram_fill{};

    std::optional<app::PathText> save{};

    std::chrono::seconds linger = kDefaultLinger;
    app::VsyncAdjust vsync_adjust = app::VsyncAdjust::Off;
    bool stay = false;
    bool strict = false;
    bool no_splash = false;
    bool loop = false;
    bool hashes_only = false;
    app::capture::RecOptions rec{};
};

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept;
void tasty_print_usage(std::FILE* out = stderr) noexcept;

}
