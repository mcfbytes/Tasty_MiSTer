// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include "app/path_text.h"
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

struct TastyArgs {
    TastyVerb verb = TastyVerb::Play;
    app::PathText movie{};
    app::PathText rom{};
    app::PathText core{};
    app::PathText record{};
    std::optional<std::int32_t> lead{};
    std::optional<std::uint32_t> stop_at{};
    std::uint32_t return_after_s = 30;
    std::uint8_t vsync_adjust = 0;
    bool stay = false;
    bool strict = false;
    bool no_splash = false;
    bool hashes = false;
    bool hashes_only = false;
};

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept;
[[nodiscard]] const char* tasty_verb_name(TastyVerb v) noexcept;
void tasty_print_usage(std::FILE* out = stderr) noexcept;

}  // namespace mister::fw
