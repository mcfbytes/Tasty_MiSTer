// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/play_refusal.h"
#include "cores/tas/movie_codec.h"

namespace mister::fw {

struct ReplayRefusal {
    app::PlayRefusal::Why why{};
    std::optional<cores::tas::MovieRefusal> movie{};

    std::string_view setting{};
    bool setting_offered = false;

    std::string_view path{};
};

}
