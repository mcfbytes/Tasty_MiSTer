// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/config.h"

namespace mister::svc {

struct ConfigSnapshot {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t keyrah_mode = 0;
    std::uint8_t forced_scandoubler = 0;
    std::uint8_t key_menu_as_rgui = 0;
    std::uint8_t reset_combo = 0;
    std::uint8_t csync = 1;
    std::uint8_t vga_scaler = 0;
    std::uint8_t vga_sog = 0;
    std::uint8_t hdmi_audio_96k = 0;
    std::uint8_t dvi_mode = 2;
    std::uint8_t hdmi_limited = 0;
    std::uint8_t hdmi_cec = 0;
    std::uint8_t hdmi_cec_sleep = 0;
    std::uint8_t hdmi_cec_wake = 0;
    std::uint8_t hdmi_cec_input_mode = 1;
    std::uint8_t hdmi_cec_power_on = 1;
    float hdmi_cec_clock = 0.0f;
    std::uint8_t direct_video = 0;
    std::uint8_t video_info = 0;
    float refresh_min = 0.0f;
    float refresh_max = 0.0f;
    std::uint8_t controller_info = 6;
    std::uint8_t vsync_adjust = 0;
    std::uint8_t kbd_nomouse = 0;
    std::uint8_t mouse_throttle = 0;
    std::uint8_t bootscreen = 1;
    std::uint8_t vscale_mode = 0;
    std::uint16_t vscale_border = 0;
    std::uint8_t rbf_hide_datecode = 0;
    std::uint8_t menu_pal = 0;
    std::int16_t bootcore_timeout = 0;
    std::uint8_t fb_size = 0;
    std::uint8_t fb_terminal = 1;
    std::uint8_t osd_rotate = 0;
    std::uint16_t osd_timeout = 0;
    std::uint8_t gamepad_defaults = 0;
    std::uint8_t recents = 0;
    std::uint16_t jamma_vid = 0;
    std::uint16_t jamma_pid = 0;
    std::uint16_t jamma2_vid = 0;
    std::uint16_t jamma2_pid = 0;
    std::uint16_t no_merge_vid = 0;
    std::uint16_t no_merge_pid = 0;
    std::uint32_t no_merge_vidpid[256]{};
    std::uint16_t spinner_vid = 0;
    std::uint16_t spinner_pid = 0;
    std::int32_t spinner_throttle = 0;
    std::uint8_t spinner_axis = 0;
    std::uint8_t sniper_mode = 0;
    std::uint8_t browse_expand = 1;
    std::uint8_t logo = 1;
    std::uint8_t log_file_entry = 0;
    std::uint8_t shmask_mode_default = 0;
    std::int32_t bt_auto_disconnect = 0;
    std::uint8_t bt_reset_before_pair = 0;
    char bootcore[256]{};
    char video_mode[1024]{};
    char video_mode_pal[1024]{};
    char video_mode_ntsc[1024]{};
    char font[1024]{};
    char shared_folder[1024]{};
    char waitmount[1024]{};
    char custom_aspect_ratio[2][16]{};
    char afilter_default[1023]{};
    char vfilter_default[1023]{};
    char vfilter_vertical_default[1023]{};
    char vfilter_scanlines_default[1023]{};
    char shmask_default[1023]{};
    char preset_default[1023]{};
    char player_controller[6][8][256]{};
    char controller_deadzone[32][256]{};
    std::uint8_t rumble = 1;
    std::uint8_t wheel_force = 50;
    std::uint16_t wheel_range = 0;
    std::uint8_t hdmi_game_mode = 0;
    std::uint8_t vrr_mode = 0;
    std::uint8_t vrr_vesa_framerate = 0;
    std::uint16_t video_off = 0;
    std::uint8_t video_off_logo = 0;
    std::uint8_t disable_autofire = 0;
    std::uint8_t video_brightness = 50;
    std::uint8_t video_contrast = 50;
    std::uint8_t video_saturation = 100;
    std::uint16_t video_hue = 0;
    char video_gain_offset[256] = "1, 0, 1, 0, 1, 0";
    std::uint8_t hdr = 0;
    std::uint16_t hdr_max_nits = 1000;
    std::uint16_t hdr_avg_nits = 250;
    char vga_mode[16]{};
    std::uint8_t vga_mode_int = 0;
    std::uint8_t ntsc_mode = 0;
    std::uint32_t controller_unique_mapping[256]{};
    char osd_lock[25]{};
    std::uint16_t osd_lock_time = 0;
    std::uint8_t debug = 0;
    std::uint8_t lookahead = 1;
    char main[1024] = "MiSTer";
    char vfilter_interlace_default[1023]{};
    char autofire_rates[3072] = "10,15,30";
    std::uint8_t autofire_on_directions = 0;
    char screenshot_image_format[16] = "png";
    std::uint16_t xbe2_shift = 0;
    std::uint8_t spd_quirk = 0;
    std::uint16_t hdmi_off = 0;
    std::uint32_t keyboard_as_joystick[256]{};

    std::uint8_t sanity_check = 0;

    std::uint8_t scanrate_lock = 0;

    std::uint8_t alt_ini = 0;
    bool second_pass_ran = false;

    std::uint8_t parse_error_count = 0;
    ParseError parse_errors[kMaxParseErrors]{};
};

[[nodiscard]] constexpr std::uint8_t direct_video_resolved(const ConfigSnapshot& c) noexcept {
    return c.direct_video == 2 ? std::uint8_t{0} : c.direct_video;
}

}  // namespace mister::svc
