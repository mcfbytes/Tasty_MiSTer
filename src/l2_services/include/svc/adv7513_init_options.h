// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::svc::adv7513 {

struct InitOptions {
    bool hdmi_game_mode = false;
    bool hdr_mode = false;
    bool hlg = false;
    bool hdmi_limited = false;
    bool ypbpr = false;
    bool dvi_mode = false;
    bool hdmi_audio_96k = false;
    bool has_hdmi_int = false;
};

}  // namespace mister::svc::adv7513
