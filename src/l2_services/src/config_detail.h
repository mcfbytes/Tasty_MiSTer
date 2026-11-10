// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

#include "infra/indexed_array.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"

namespace mister::svc::detail {

#define TASTY_CFG_OFF(field) static_cast<std::uint16_t>(offsetof(ConfigSnapshot, field))

constexpr std::size_t kPlayerSlots = 8;
constexpr std::size_t kPlayerBytes = 256;
constexpr std::size_t kPlayerStride = kPlayerSlots * kPlayerBytes;

constexpr std::uint16_t player_off(std::size_t n) {
    return static_cast<std::uint16_t>(
        offsetof(ConfigSnapshot, player_controller) + n * kPlayerStride
    );
}

constexpr std::uint16_t aspect_off(std::size_t n) {
    return static_cast<std::uint16_t>(offsetof(ConfigSnapshot, custom_aspect_ratio) + n * 16u);
}

struct RowId {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr RowId classic(std::size_t j) noexcept {
        return RowId{static_cast<std::uint16_t>(j)};
    }

    [[nodiscard]] static constexpr RowId extension(std::size_t j) noexcept {
        return RowId{static_cast<std::uint16_t>(kNumIniOptions + j)};
    }

    friend constexpr bool operator==(RowId, RowId) = default;
};

constexpr std::int64_t kU32Max = std::numeric_limits<std::uint32_t>::max();
constexpr std::int64_t kU16Max = 0xFFFF;

inline constexpr Option kSchema[] = {

    {"YPBPR", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(vga_mode_int), 0, false},
    {"COMPOSITE_SYNC", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(csync), 0, false},
    {"FORCED_SCANDOUBLER", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(forced_scandoubler), 0, false},
    {"VGA_SCALER", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(vga_scaler), 0, false},
    {"VGA_SOG", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(vga_sog), 0, false},
    {"KEYRAH_MODE", OptionType::Hex32, 0, kU32Max, 0, TASTY_CFG_OFF(keyrah_mode), 0, false},
    {"RESET_COMBO", OptionType::U8, 0, 3, 0, TASTY_CFG_OFF(reset_combo), 0, false},
    {"KEY_MENU_AS_RGUI", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(key_menu_as_rgui), 0, false},
    {"VIDEO_MODE", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(video_mode), 1024, false},
    {"VIDEO_MODE_PAL", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(video_mode_pal), 1024, false},
    {"VIDEO_MODE_NTSC", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(video_mode_ntsc), 1024, false},
    {"VIDEO_INFO", OptionType::U8, 0, 10, 0, TASTY_CFG_OFF(video_info), 0, false},
    {"VSYNC_ADJUST", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(vsync_adjust), 0, false},
    {"HDMI_AUDIO_96K", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hdmi_audio_96k), 0, false},

    {"DVI_MODE", OptionType::U8, 0, 1, 2, TASTY_CFG_OFF(dvi_mode), 0, true},
    {"HDMI_LIMITED", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(hdmi_limited), 0, false},
    {"HDMI_CEC", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hdmi_cec), 0, false},
    {"HDMI_CEC_SLEEP", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hdmi_cec_sleep), 0, false},
    {"HDMI_CEC_WAKE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hdmi_cec_wake), 0, false},
    {"HDMI_CEC_INPUT_MODE", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(hdmi_cec_input_mode), 0, false},
    {"HDMI_CEC_POWER_ON", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(hdmi_cec_power_on), 0, false},
    {"HDMI_CEC_CLOCK", OptionType::F32, 0, 100, 0, TASTY_CFG_OFF(hdmi_cec_clock), 0, false},
    {"KBD_NOMOUSE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(kbd_nomouse), 0, false},

    {"MOUSE_THROTTLE", OptionType::U8, 1, 100, 0, TASTY_CFG_OFF(mouse_throttle), 0, true},
    {"BOOTSCREEN", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(bootscreen), 0, false},
    {"VSCALE_MODE", OptionType::U8, 0, 5, 0, TASTY_CFG_OFF(vscale_mode), 0, false},
    {"VSCALE_BORDER", OptionType::U16, 0, 399, 0, TASTY_CFG_OFF(vscale_border), 0, false},
    {"RBF_HIDE_DATECODE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(rbf_hide_datecode), 0, false},
    {"MENU_PAL", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(menu_pal), 0, false},
    {"BOOTCORE", OptionType::Str, 0, 255, 0, TASTY_CFG_OFF(bootcore), 256, false},

    {"BOOTCORE_TIMEOUT", OptionType::I16, 2, 30, 0, TASTY_CFG_OFF(bootcore_timeout), 0, true},
    {"FONT", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(font), 1024, false},
    {"FB_SIZE", OptionType::U8, 0, 4, 0, TASTY_CFG_OFF(fb_size), 0, false},
    {"FB_TERMINAL", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(fb_terminal), 0, false},
    {"OSD_TIMEOUT", OptionType::I16, 0, 3600, 0, TASTY_CFG_OFF(osd_timeout), 0, false},
    {"DIRECT_VIDEO", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(direct_video), 0, false},
    {"OSD_ROTATE", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(osd_rotate), 0, false},
    {"DEADZONE", OptionType::StrArr, 32, 256, 0, TASTY_CFG_OFF(controller_deadzone), 8192, false},
    {"GAMEPAD_DEFAULTS", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(gamepad_defaults), 0, false},
    {"RECENTS", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(recents), 0, false},
    {"CONTROLLER_INFO", OptionType::U8, 0, 10, 6, TASTY_CFG_OFF(controller_info), 0, false},
    {"REFRESH_MIN", OptionType::F32, 0, 150, 0, TASTY_CFG_OFF(refresh_min), 0, false},
    {"REFRESH_MAX", OptionType::F32, 0, 150, 0, TASTY_CFG_OFF(refresh_max), 0, false},
    {"JAMMA_VID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(jamma_vid), 0, false},
    {"JAMMA_PID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(jamma_pid), 0, false},
    {"JAMMA2_VID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(jamma2_vid), 0, false},
    {"JAMMA2_PID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(jamma2_pid), 0, false},
    {"SNIPER_MODE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(sniper_mode), 0, false},
    {"BROWSE_EXPAND", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(browse_expand), 0, false},
    {"LOGO", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(logo), 0, false},
    {"SHARED_FOLDER", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(shared_folder), 1024, false},
    {"NO_MERGE_VID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(no_merge_vid), 0, false},
    {"NO_MERGE_PID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(no_merge_pid), 0, false},
    {"NO_MERGE_VIDPID",
     OptionType::Hex32Arr,
     0,
     kU32Max,
     0,
     TASTY_CFG_OFF(no_merge_vidpid),
     1024,
     false},
    {"CUSTOM_ASPECT_RATIO_1", OptionType::Str, 0, 15, 0, aspect_off(0), 16, false},
    {"CUSTOM_ASPECT_RATIO_2", OptionType::Str, 0, 15, 0, aspect_off(1), 16, false},
    {"SPINNER_VID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(spinner_vid), 0, false},
    {"SPINNER_PID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(spinner_pid), 0, false},
    {"SPINNER_AXIS", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(spinner_axis), 0, false},
    {"SPINNER_THROTTLE",
     OptionType::I32,
     -10000,
     10000,
     0,
     TASTY_CFG_OFF(spinner_throttle),
     0,
     false},
    {"AFILTER_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(afilter_default), 1023, false},
    {"VFILTER_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(vfilter_default), 1023, false},
    {"VFILTER_VERTICAL_DEFAULT",
     OptionType::Str,
     0,
     1022,
     0,
     TASTY_CFG_OFF(vfilter_vertical_default),
     1023,
     false},
    {"VFILTER_SCANLINES_DEFAULT",
     OptionType::Str,
     0,
     1022,
     0,
     TASTY_CFG_OFF(vfilter_scanlines_default),
     1023,
     false},
    {"SHMASK_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(shmask_default), 1023, false},
    {"SHMASK_MODE_DEFAULT",
     OptionType::U8,
     0,
     255,
     0,
     TASTY_CFG_OFF(shmask_mode_default),
     0,
     false},
    {"PRESET_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(preset_default), 1023, false},
    {"LOG_FILE_ENTRY", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(log_file_entry), 0, false},
    {"BT_AUTO_DISCONNECT", OptionType::U32, 0, 180, 0, TASTY_CFG_OFF(bt_auto_disconnect), 0, false},
    {"BT_RESET_BEFORE_PAIR",
     OptionType::U8,
     0,
     1,
     0,
     TASTY_CFG_OFF(bt_reset_before_pair),
     0,
     false},
    {"WAITMOUNT", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(waitmount), 1024, false},
    {"RUMBLE", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(rumble), 0, false},
    {"WHEEL_FORCE", OptionType::U8, 0, 100, 50, TASTY_CFG_OFF(wheel_force), 0, false},
    {"WHEEL_RANGE", OptionType::U16, 0, 1000, 0, TASTY_CFG_OFF(wheel_range), 0, false},
    {"HDMI_GAME_MODE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hdmi_game_mode), 0, false},
    {"VRR_MODE", OptionType::U8, 0, 4, 0, TASTY_CFG_OFF(vrr_mode), 0, false},
    {"VRR_VESA_FRAMERATE", OptionType::U8, 0, 255, 0, TASTY_CFG_OFF(vrr_vesa_framerate), 0, false},
    {"VIDEO_OFF", OptionType::I16, 0, 3600, 0, TASTY_CFG_OFF(video_off), 0, false},
    {"VIDEO_OFF_LOGO", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(video_off_logo), 0, false},
    {"PLAYER_1_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(0), 2048, false},
    {"PLAYER_2_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(1), 2048, false},
    {"PLAYER_3_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(2), 2048, false},
    {"PLAYER_4_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(3), 2048, false},
    {"PLAYER_5_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(4), 2048, false},
    {"PLAYER_6_CONTROLLER", OptionType::StrArr, 8, 256, 0, player_off(5), 2048, false},
    {"DISABLE_AUTOFIRE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(disable_autofire), 0, false},
    {"VIDEO_BRIGHTNESS", OptionType::U8, 0, 100, 50, TASTY_CFG_OFF(video_brightness), 0, false},
    {"VIDEO_CONTRAST", OptionType::U8, 0, 100, 50, TASTY_CFG_OFF(video_contrast), 0, false},
    {"VIDEO_SATURATION", OptionType::U8, 0, 100, 100, TASTY_CFG_OFF(video_saturation), 0, false},
    {"VIDEO_HUE", OptionType::U16, 0, 360, 0, TASTY_CFG_OFF(video_hue), 0, false},

    {"VIDEO_GAIN_OFFSET", OptionType::Str, 0, 256, 0, TASTY_CFG_OFF(video_gain_offset), 256, false},
    {"HDR", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(hdr), 0, false},
    {"HDR_MAX_NITS", OptionType::U16, 100, 10000, 1000, TASTY_CFG_OFF(hdr_max_nits), 0, false},
    {"HDR_AVG_NITS", OptionType::U16, 100, 10000, 250, TASTY_CFG_OFF(hdr_avg_nits), 0, false},
    {"VGA_MODE", OptionType::Str, 0, 15, 0, TASTY_CFG_OFF(vga_mode), 16, false},
    {"NTSC_MODE", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(ntsc_mode), 0, false},
    {"CONTROLLER_UNIQUE_MAPPING",
     OptionType::Array,
     0,
     kU32Max,
     0,
     TASTY_CFG_OFF(controller_unique_mapping),
     1024,
     false},
    {"OSD_LOCK", OptionType::Str, 0, 24, 0, TASTY_CFG_OFF(osd_lock), 25, false},
    {"OSD_LOCK_TIME", OptionType::U16, 0, 60, 0, TASTY_CFG_OFF(osd_lock_time), 0, false},
    {"DEBUG", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(debug), 0, false},
    {"LOOKAHEAD", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(lookahead), 0, false},
    {"MAIN", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(main), 1024, false},
    {"VFILTER_INTERLACE_DEFAULT",
     OptionType::Str,
     0,
     1022,
     0,
     TASTY_CFG_OFF(vfilter_interlace_default),
     1023,
     false},
    {"AUTOFIRE_RATES", OptionType::Str, 0, 3071, 0, TASTY_CFG_OFF(autofire_rates), 3072, false},
    {"AUTOFIRE_ON_DIRECTIONS",
     OptionType::U8,
     0,
     1,
     0,
     TASTY_CFG_OFF(autofire_on_directions),
     0,
     false},
    {"SCREENSHOT_IMAGE_FORMAT",
     OptionType::Str,
     0,
     15,
     0,
     TASTY_CFG_OFF(screenshot_image_format),
     16,
     false},
    {"XBE2_SHIFT", OptionType::U16, 0, 0x22F, 0, TASTY_CFG_OFF(xbe2_shift), 0, false},
    {"SPD_QUIRK", OptionType::U8, 0, 3, 0, TASTY_CFG_OFF(spd_quirk), 0, false},
    {"HDMI_OFF", OptionType::U16, 0, 1440, 0, TASTY_CFG_OFF(hdmi_off), 0, false},
    {"KEYBOARD_AS_JOYSTICK",
     OptionType::Hex32Arr,
     0,
     kU32Max,
     0,
     TASTY_CFG_OFF(keyboard_as_joystick),
     1024,
     false},
    {"SANITY_CHECK", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(sanity_check), 0, false},
};

inline constexpr Option kExtSchema[] = {
    {"SCANRATE_LOCK", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(scanrate_lock), 0, false},
    {"OSD_STYLE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(osd_style), 0, false},
    {"HD_OSD_FPS", OptionType::U8, 1, 30, 10, TASTY_CFG_OFF(hd_osd_fps), 0, false},
    {"HD_THEME", OptionType::U8, 0, 255, 0, TASTY_CFG_OFF(hd_theme), 0, false},
    {"HD_LAYOUT", OptionType::U8, 0, 255, 255, TASTY_CFG_OFF(hd_layout), 0, false},
    {"HD_COLORS", OptionType::U8, 0, 255, 255, TASTY_CFG_OFF(hd_colors), 0, false},
    {"HD_GAME_DISPLAY", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(hd_game_display), 0, false},
    {"HD_MENU_OPACITY", OptionType::U8, 50, 100, 85, TASTY_CFG_OFF(hd_menu_opacity), 0, false},
};

template <class F>
consteval bool def_matches(const Option& o, const F& field) {
    if constexpr (std::is_floating_point_v<F>) {
        return o.def == 0 && field == F{};
    } else {
        return o.def == field;
    }
}

consteval bool default_agrees(const ConfigSnapshot& d, const Option& o) {
#define TASTY_CFG_CHECK(field) \
    if (o.offset == TASTY_CFG_OFF(field)) \
        return def_matches(o, d.field);

    TASTY_CFG_CHECK(keyrah_mode)
    TASTY_CFG_CHECK(forced_scandoubler)
    TASTY_CFG_CHECK(key_menu_as_rgui)
    TASTY_CFG_CHECK(reset_combo)
    TASTY_CFG_CHECK(csync)
    TASTY_CFG_CHECK(vga_scaler)
    TASTY_CFG_CHECK(vga_sog)
    TASTY_CFG_CHECK(hdmi_audio_96k)
    TASTY_CFG_CHECK(dvi_mode)
    TASTY_CFG_CHECK(hdmi_limited)
    TASTY_CFG_CHECK(hdmi_cec)
    TASTY_CFG_CHECK(hdmi_cec_sleep)
    TASTY_CFG_CHECK(hdmi_cec_wake)
    TASTY_CFG_CHECK(hdmi_cec_input_mode)
    TASTY_CFG_CHECK(hdmi_cec_power_on)
    TASTY_CFG_CHECK(hdmi_cec_clock)
    TASTY_CFG_CHECK(direct_video)
    TASTY_CFG_CHECK(video_info)
    TASTY_CFG_CHECK(refresh_min)
    TASTY_CFG_CHECK(refresh_max)
    TASTY_CFG_CHECK(controller_info)
    TASTY_CFG_CHECK(vsync_adjust)
    TASTY_CFG_CHECK(kbd_nomouse)
    TASTY_CFG_CHECK(mouse_throttle)
    TASTY_CFG_CHECK(bootscreen)
    TASTY_CFG_CHECK(vscale_mode)
    TASTY_CFG_CHECK(vscale_border)
    TASTY_CFG_CHECK(rbf_hide_datecode)
    TASTY_CFG_CHECK(menu_pal)
    TASTY_CFG_CHECK(bootcore_timeout)
    TASTY_CFG_CHECK(fb_size)
    TASTY_CFG_CHECK(fb_terminal)
    TASTY_CFG_CHECK(osd_rotate)
    TASTY_CFG_CHECK(osd_timeout)
    TASTY_CFG_CHECK(gamepad_defaults)
    TASTY_CFG_CHECK(recents)
    TASTY_CFG_CHECK(jamma_vid)
    TASTY_CFG_CHECK(jamma_pid)
    TASTY_CFG_CHECK(jamma2_vid)
    TASTY_CFG_CHECK(jamma2_pid)
    TASTY_CFG_CHECK(no_merge_vid)
    TASTY_CFG_CHECK(no_merge_pid)
    TASTY_CFG_CHECK(spinner_vid)
    TASTY_CFG_CHECK(spinner_pid)
    TASTY_CFG_CHECK(spinner_throttle)
    TASTY_CFG_CHECK(spinner_axis)
    TASTY_CFG_CHECK(sniper_mode)
    TASTY_CFG_CHECK(browse_expand)
    TASTY_CFG_CHECK(logo)
    TASTY_CFG_CHECK(log_file_entry)
    TASTY_CFG_CHECK(shmask_mode_default)
    TASTY_CFG_CHECK(bt_auto_disconnect)
    TASTY_CFG_CHECK(bt_reset_before_pair)
    TASTY_CFG_CHECK(rumble)
    TASTY_CFG_CHECK(wheel_force)
    TASTY_CFG_CHECK(wheel_range)
    TASTY_CFG_CHECK(hdmi_game_mode)
    TASTY_CFG_CHECK(vrr_mode)
    TASTY_CFG_CHECK(vrr_vesa_framerate)
    TASTY_CFG_CHECK(video_off)
    TASTY_CFG_CHECK(video_off_logo)
    TASTY_CFG_CHECK(disable_autofire)
    TASTY_CFG_CHECK(video_brightness)
    TASTY_CFG_CHECK(video_contrast)
    TASTY_CFG_CHECK(video_saturation)
    TASTY_CFG_CHECK(video_hue)
    TASTY_CFG_CHECK(hdr)
    TASTY_CFG_CHECK(hdr_max_nits)
    TASTY_CFG_CHECK(hdr_avg_nits)
    TASTY_CFG_CHECK(vga_mode_int)
    TASTY_CFG_CHECK(ntsc_mode)
    TASTY_CFG_CHECK(osd_lock_time)
    TASTY_CFG_CHECK(debug)
    TASTY_CFG_CHECK(lookahead)
    TASTY_CFG_CHECK(autofire_on_directions)
    TASTY_CFG_CHECK(xbe2_shift)
    TASTY_CFG_CHECK(spd_quirk)
    TASTY_CFG_CHECK(hdmi_off)
    TASTY_CFG_CHECK(sanity_check)
    TASTY_CFG_CHECK(scanrate_lock)
    TASTY_CFG_CHECK(osd_style)
    TASTY_CFG_CHECK(hd_osd_fps)
    TASTY_CFG_CHECK(hd_theme)
    TASTY_CFG_CHECK(hd_layout)
    TASTY_CFG_CHECK(hd_colors)
    TASTY_CFG_CHECK(hd_game_display)
    TASTY_CFG_CHECK(hd_menu_opacity)
    TASTY_CFG_CHECK(alt_ini)
    TASTY_CFG_CHECK(parse_error_count)
#undef TASTY_CFG_CHECK
    return o.def == 0;
}

consteval bool defaults_agree() {

    const ConfigSnapshot d{};
    const auto agrees = [&d](const Option& o) {
        if (o.length != 0)
            return true;
        return default_agrees(d, o);
    };
    for (const Option& o : kSchema) {
        if (!agrees(o))
            return false;
    }
    for (const Option& o : kExtSchema) {
        if (!agrees(o))
            return false;
    }
    return true;
}

static_assert(defaults_agree(), "Option::def must agree with ConfigSnapshot's initializers");

#undef TASTY_CFG_OFF

[[nodiscard]] constexpr const Option& option_at(RowId id) noexcept {
    return (id.v < kNumIniOptions) ? kSchema[id.v] : kExtSchema[id.v - kNumIniOptions];
}

using ArrayAppendSeen = infra::IndexedArray<bool, kNumIniOptions + kNumExtOptions, RowId>;

}
