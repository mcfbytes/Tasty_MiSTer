// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>

#include "svc/adv7513_init_options.h"
#include "svc/config_snapshot.h"
#include "app/types.h"
#include "svc/modeline.h"
#include "svc/video_sample.h"

namespace mister::app {

using AspectWords = std::array<std::uint16_t, 2>;

struct AspectTerm {
    std::uint16_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr std::optional<AspectTerm> of(I n) noexcept {
        if (n < 1 || n > svc::AspectRaw::kValueMask)
            return std::nullopt;
        return AspectTerm{static_cast<std::uint16_t>(n)};
    }

    friend constexpr AspectWords aspect_words(AspectTerm num, AspectTerm den) noexcept {
        return {num.v, den.v};
    }

    friend constexpr bool operator==(AspectTerm, AspectTerm) = default;
};

struct AspectRatio {
    AspectTerm num{};
    AspectTerm den{};
    friend constexpr bool operator==(AspectRatio, AspectRatio) = default;
};

struct VfilterSeeds {
    std::string horz{};
    std::string vert{};
    std::string scan{};
    std::string ilace{};
};

struct VideoIni {

    struct Force {
        bool replay = false;
        bool strict_direct_video = false;
        std::optional<VsyncAdjust> vsync{};
    };

    svc::Modeline::WireOptions wire{};
    svc::adv7513::InitOptions init{};
    std::uint8_t spd_quirk = 0;

    VfilterSeeds vfilter_seeds{};

    std::string spec_pal{};
    std::string spec_ntsc{};
    VsyncAdjust vsync_adjust = VsyncAdjust::Off;
    double refresh_min = 0.0;
    double refresh_max = 0.0;
    bool direct_video = false;
    std::uint8_t direct_video_ini = 0;
    bool menu_pal = false;
    bool forced_scandoubler = false;

    std::array<AspectRatio, 2> ar_cust{};
};

[[nodiscard]] VideoIni parse_video_ini(
    const svc::ConfigSnapshot& snap,
    const VideoIni::Force& force
);

[[nodiscard]] VideoIni absent_video_ini(const VideoIni::Force& force, const VideoIni& last);

[[nodiscard]] constexpr std::array<std::uint16_t, 4> ar_custom_words(
    const std::array<AspectRatio, 2>& ratios
) noexcept {
    const auto first = aspect_words(ratios[0].num, ratios[0].den);
    const auto second = aspect_words(ratios[1].num, ratios[1].den);
    return {first[0], first[1], second[0], second[1]};
}

}
