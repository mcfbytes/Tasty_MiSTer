// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/video/adv7513_hdr_mode.h"

namespace mister::svc::video::adv7513 {

struct InitOptions {
    bool hdmi_game_mode = false;
    HdrMode hdr = HdrMode::Off;
    bool hdmi_limited = false;
    bool ypbpr = false;
    bool dvi_mode = false;
    bool hdmi_audio_96k = false;
    bool has_hdmi_int = false;
};

}
