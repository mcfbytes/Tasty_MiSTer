// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "app/play_refusal.h"
#include "cores/movie_codec.h"
#include "infra/error.h"
#include "tasty_refusal.h"

namespace mister::fw {

[[nodiscard]] const char* tasty_refusal_sentence(
    const ReplayRefusal& refusal,
    std::string_view movie,
    bool strict,
    std::span<char> out
) noexcept;
[[nodiscard]] const char* tasty_movie_refusal_sentence(cores::MovieRefusal why) noexcept;

void report_unhashable(
    const cores::MovieCodec& codec,
    std::string_view rom,
    const Error& e
) noexcept;

}
