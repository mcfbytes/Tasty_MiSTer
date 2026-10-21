// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_ini.h"

#include <cstddef>
#include <cstdio>

namespace mister::app {

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

    ini.direct_video_ini = svc::direct_video_resolved(snap);

    const bool dv_on = ini.direct_video_ini != 0 && !(force.replay && !force.strict_direct_video);
    ini.wire.direct_video = dv_on;
    ini.spd_quirk = snap.spd_quirk;

    for (std::size_t i = 0; i < 2; ++i) {
        std::uint32_t num = 0, den = 0;
        const char* t = snap.custom_aspect_ratio[i];
        if (t[0] != '\0'
            && std::sscanf(t, "%u:%u", &num, &den) == 2
            && num >= 1
            && num <= 4095
            && den >= 1
            && den <= 4095) {
            ini.ar_cust[i * 2] = static_cast<std::uint16_t>(num);
            ini.ar_cust[i * 2 + 1] = static_cast<std::uint16_t>(den);
        } else {
            ini.ar_cust[i * 2] = 0;
            ini.ar_cust[i * 2 + 1] = 0;
        }
    }

    ini.vfilter_seeds[0] = snap.vfilter_default;
    ini.vfilter_seeds[1] = snap.vfilter_vertical_default;
    ini.vfilter_seeds[2] = snap.vfilter_scanlines_default;
    ini.vfilter_seeds[3] = snap.vfilter_interlace_default;

    ini.spec_pal = snap.video_mode_pal;
    ini.spec_ntsc = snap.video_mode_ntsc;
    ini.menu_pal = snap.menu_pal != 0;
    ini.forced_scandoubler = snap.forced_scandoubler != 0;
    ini.direct_video = dv_on;
    ini.vsync_adjust = snap.vsync_adjust;

    if (ini.direct_video)
        ini.vsync_adjust = 0;
    if (force.vsync && !ini.direct_video) {
        ini.vsync_adjust = *force.vsync;
        ini.wire.vsync_align = false;
    } else {
        ini.wire.vsync_align = (ini.vsync_adjust == 2);
    }
    ini.refresh_min = snap.refresh_min;
    ini.refresh_max = snap.refresh_max;

    ini.init.hdmi_game_mode = snap.hdmi_game_mode != 0;
    ini.init.hdr_mode = snap.hdr != 0;
    ini.init.hlg = snap.hdr == 1;
    ini.init.hdmi_limited = snap.hdmi_limited != 0;
    ini.init.ypbpr = (snap.vga_mode_int == 1) && ini.direct_video && ini.direct_video_ini == 1;
    ini.init.dvi_mode = snap.dvi_mode == 1;
    ini.init.hdmi_audio_96k = snap.hdmi_audio_96k != 0;

    ini.init.has_hdmi_int = false;
    return ini;
}

}
