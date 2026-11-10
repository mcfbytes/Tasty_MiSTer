// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_ini.h"

#include <cstddef>
#include <cstdio>

namespace mister::app {

namespace {

VsyncAdjust vsync_adjust_of(const svc::ConfigSnapshot& snap) noexcept {
    constexpr std::uint8_t kOff = 0;
    constexpr std::uint8_t kMatch = 1;
    if (snap.vsync_adjust == kOff)
        return VsyncAdjust::Off;
    if (snap.vsync_adjust == kMatch)
        return VsyncAdjust::Match;
    return VsyncAdjust::LowLag;
}

svc::adv7513::HdrMode hdr_mode_of(const svc::ConfigSnapshot& snap) noexcept {
    constexpr std::uint8_t kOff = 0;
    constexpr std::uint8_t kHlg = 1;
    if (snap.hdr == kOff)
        return svc::adv7513::HdrMode::Off;
    if (snap.hdr == kHlg)
        return svc::adv7513::HdrMode::Hlg;
    return svc::adv7513::HdrMode::DciP3;
}

[[nodiscard]] AspectRatio aspect_ratio_of(const char* text) noexcept {
    unsigned num = 0;
    unsigned den = 0;
    if (text[0] == '\0' || std::sscanf(text, "%u:%u", &num, &den) != 2)
        return AspectRatio{};
    const auto numerator = AspectTerm::of(num);
    const auto denominator = AspectTerm::of(den);
    if (!numerator || !denominator)
        return AspectRatio{};
    return AspectRatio{*numerator, *denominator};
}

}

VideoIni absent_video_ini(const VideoIni::Force& force, const VideoIni& last) {
    VideoIni ini{};
    ini.ar_cust = last.ar_cust;
    if (force.vsync) {
        ini.vsync_adjust = *force.vsync;
        ini.wire.vsync_align = false;
    }
    return ini;
}

VideoIni parse_video_ini(const svc::ConfigSnapshot& snap, const VideoIni::Force& force) {
    VideoIni ini{};

    ini.direct_video_ini = static_cast<std::uint8_t>(svc::direct_video_resolved(snap));

    const bool dv_on = ini.direct_video_ini != 0 && !(force.replay && !force.strict_direct_video);
    ini.wire.direct_video = dv_on;
    ini.spd_quirk = snap.spd_quirk;

    for (std::size_t i = 0; i < ini.ar_cust.size(); ++i)
        ini.ar_cust[i] = aspect_ratio_of(snap.custom_aspect_ratio[i]);

    ini.vfilter_seeds.horz = snap.vfilter_default;
    ini.vfilter_seeds.vert = snap.vfilter_vertical_default;
    ini.vfilter_seeds.scan = snap.vfilter_scanlines_default;
    ini.vfilter_seeds.ilace = snap.vfilter_interlace_default;

    ini.spec_pal = snap.video_mode_pal;
    ini.spec_ntsc = snap.video_mode_ntsc;
    ini.menu_pal = snap.menu_pal != 0;
    ini.forced_scandoubler = snap.forced_scandoubler != 0;
    ini.direct_video = dv_on;
    ini.vsync_adjust = vsync_adjust_of(snap);

    if (ini.direct_video)
        ini.vsync_adjust = VsyncAdjust::Off;
    if (force.vsync && !ini.direct_video) {
        ini.vsync_adjust = *force.vsync;
        ini.wire.vsync_align = false;
    } else {
        ini.wire.vsync_align = (ini.vsync_adjust == VsyncAdjust::LowLag);
    }
    ini.refresh_min = snap.refresh_min;
    ini.refresh_max = snap.refresh_max;

    ini.init.hdmi_game_mode = snap.hdmi_game_mode != 0;
    ini.init.hdr = hdr_mode_of(snap);
    ini.init.hdmi_limited = snap.hdmi_limited != 0;
    ini.init.ypbpr = (vga_mode_of(snap.vga_mode_int) == VgaMode::YPbPr)
        && ini.direct_video
        && ini.direct_video_ini == 1;
    ini.init.dvi_mode = snap.dvi_mode == 1;
    ini.init.hdmi_audio_96k = snap.hdmi_audio_96k != 0;

    ini.init.has_hdmi_int = false;
    return ini;
}

}
