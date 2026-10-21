// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "svc/adv7513_init_options.h"
#include "svc/config_snapshot.h"
#include "svc/modeline.h"

namespace mister::app {

struct VideoIni {

    struct Force {
        bool replay = false;
        bool strict_direct_video = false;
        std::optional<std::uint8_t> vsync{};
    };

    svc::Modeline::WireOptions wire{};
    svc::adv7513::InitOptions init{};
    std::uint8_t spd_quirk = 0;

    std::array<std::string, 4> vfilter_seeds{};

    std::string spec_pal{};
    std::string spec_ntsc{};
    std::uint8_t vsync_adjust = 0;
    double refresh_min = 0.0;
    double refresh_max = 0.0;
    bool direct_video = false;
    std::uint8_t direct_video_ini = 0;
    bool menu_pal = false;
    bool forced_scandoubler = false;
    std::array<std::uint16_t, 4> ar_cust{};
};

[[nodiscard]] VideoIni parse_video_ini(
    const svc::ConfigSnapshot& snap,
    const VideoIni::Force& force
);

[[nodiscard]] VideoIni absent_video_ini(const VideoIni::Force& force, const VideoIni& last);

}
