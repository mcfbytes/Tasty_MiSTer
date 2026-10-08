// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/config.h"

#include "svc/alt_ini_index.h"
#include "svc/config_parser.h"
#include "svc/config_snapshot.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace mister::svc {
namespace {

#define TASTY_CFG_OFF(field) static_cast<std::uint16_t>(offsetof(ConfigSnapshot, field))

constexpr std::size_t kPlayerSlots = 8;
constexpr std::size_t kPlayerBytes = 256;
constexpr std::size_t kPlayerStride = kPlayerSlots * kPlayerBytes;
constexpr std::uint16_t player_off(std::size_t n) {
    return static_cast<std::uint16_t>(offsetof(ConfigSnapshot, player_controller) +
                                      n * kPlayerStride);
}
constexpr std::uint16_t aspect_off(std::size_t n) {
    return static_cast<std::uint16_t>(offsetof(ConfigSnapshot, custom_aspect_ratio) + n * 16u);
}

constexpr std::int64_t kU32Max = 0xFFFFFFFF;
constexpr std::int64_t kU16Max = 0xFFFF;

constexpr Option kSchema[] = {

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
    {"NO_MERGE_VIDPID", OptionType::Hex32Arr, 0, kU32Max, 0, TASTY_CFG_OFF(no_merge_vidpid), 1024,
     false},
    {"CUSTOM_ASPECT_RATIO_1", OptionType::Str, 0, 15, 0, aspect_off(0), 16, false},
    {"CUSTOM_ASPECT_RATIO_2", OptionType::Str, 0, 15, 0, aspect_off(1), 16, false},
    {"SPINNER_VID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(spinner_vid), 0, false},
    {"SPINNER_PID", OptionType::Hex16, 0, kU16Max, 0, TASTY_CFG_OFF(spinner_pid), 0, false},
    {"SPINNER_AXIS", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(spinner_axis), 0, false},
    {"SPINNER_THROTTLE", OptionType::I32, -10000, 10000, 0, TASTY_CFG_OFF(spinner_throttle), 0,
     false},
    {"AFILTER_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(afilter_default), 1023, false},
    {"VFILTER_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(vfilter_default), 1023, false},
    {"VFILTER_VERTICAL_DEFAULT", OptionType::Str, 0, 1022, 0,
     TASTY_CFG_OFF(vfilter_vertical_default), 1023, false},
    {"VFILTER_SCANLINES_DEFAULT", OptionType::Str, 0, 1022, 0,
     TASTY_CFG_OFF(vfilter_scanlines_default), 1023, false},
    {"SHMASK_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(shmask_default), 1023, false},
    {"SHMASK_MODE_DEFAULT", OptionType::U8, 0, 255, 0, TASTY_CFG_OFF(shmask_mode_default), 0,
     false},
    {"PRESET_DEFAULT", OptionType::Str, 0, 1022, 0, TASTY_CFG_OFF(preset_default), 1023, false},
    {"LOG_FILE_ENTRY", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(log_file_entry), 0, false},
    {"BT_AUTO_DISCONNECT", OptionType::U32, 0, 180, 0, TASTY_CFG_OFF(bt_auto_disconnect), 0, false},
    {"BT_RESET_BEFORE_PAIR", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(bt_reset_before_pair), 0,
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
    {"CONTROLLER_UNIQUE_MAPPING", OptionType::Array, 0, kU32Max, 0,
     TASTY_CFG_OFF(controller_unique_mapping), 1024, false},
    {"OSD_LOCK", OptionType::Str, 0, 24, 0, TASTY_CFG_OFF(osd_lock), 25, false},
    {"OSD_LOCK_TIME", OptionType::U16, 0, 60, 0, TASTY_CFG_OFF(osd_lock_time), 0, false},
    {"DEBUG", OptionType::U8, 0, 2, 0, TASTY_CFG_OFF(debug), 0, false},
    {"LOOKAHEAD", OptionType::U8, 0, 1, 1, TASTY_CFG_OFF(lookahead), 0, false},
    {"MAIN", OptionType::Str, 0, 1023, 0, TASTY_CFG_OFF(main), 1024, false},
    {"VFILTER_INTERLACE_DEFAULT", OptionType::Str, 0, 1022, 0,
     TASTY_CFG_OFF(vfilter_interlace_default), 1023, false},
    {"AUTOFIRE_RATES", OptionType::Str, 0, 3071, 0, TASTY_CFG_OFF(autofire_rates), 3072, false},
    {"AUTOFIRE_ON_DIRECTIONS", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(autofire_on_directions), 0,
     false},
    {"SCREENSHOT_IMAGE_FORMAT", OptionType::Str, 0, 15, 0, TASTY_CFG_OFF(screenshot_image_format),
     16, false},
    {"XBE2_SHIFT", OptionType::U16, 0, 0x22F, 0, TASTY_CFG_OFF(xbe2_shift), 0, false},
    {"SPD_QUIRK", OptionType::U8, 0, 3, 0, TASTY_CFG_OFF(spd_quirk), 0, false},
    {"HDMI_OFF", OptionType::U16, 0, 1440, 0, TASTY_CFG_OFF(hdmi_off), 0, false},
    {"KEYBOARD_AS_JOYSTICK", OptionType::Hex32Arr, 0, kU32Max, 0,
     TASTY_CFG_OFF(keyboard_as_joystick), 1024, false},
    {"SANITY_CHECK", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(sanity_check), 0, false},
};

constexpr Option kExtSchema[] = {
    {"SCANRATE_LOCK", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(scanrate_lock), 0, false},
    {"OSD_STYLE", OptionType::U8, 0, 1, 0, TASTY_CFG_OFF(osd_style), 0, false},
    {"HD_OSD_FPS", OptionType::U8, 1, 30, 10, TASTY_CFG_OFF(hd_osd_fps), 0, false},
};

#undef TASTY_CFG_OFF

static_assert(std::size(kSchema) == kNumIniOptions, "req: ini_vars[] has exactly 111 entries");
static_assert(std::size(kExtSchema) == kNumExtOptions,
              "kNumExtOptions must count the tasty-only rows exactly");

consteval std::size_t count_sentinels() {
    std::size_t n = 0;
    for (const Option& o : kSchema) {
        if (o.sentinel_default) ++n;
    }
    return n;
}
static_assert(count_sentinels() == 3, "req: exactly three options have "
                                      "a compiled-in default outside [min,max]");

static_assert(sizeof(ConfigSnapshot) <= 0xFFFF,
              "Option::offset is uint16_t; ConfigSnapshot must stay addressable");

constexpr std::size_t type_width(OptionType t) {
    switch (t) {
        case OptionType::U8:
        case OptionType::I8:
        case OptionType::Hex8:
            return 1;
        case OptionType::U16:
        case OptionType::I16:
        case OptionType::Hex16:
            return 2;
        case OptionType::U32:
        case OptionType::I32:
        case OptionType::Hex32:
        case OptionType::F32:
            return 4;
        case OptionType::Str:
        case OptionType::StrArr:
        case OptionType::Array:
        case OptionType::Hex32Arr:
            return 0;
    }
    return 0;
}

consteval bool rows_fit(std::span<const Option> rows) {
    for (const Option& o : rows) {
        const std::size_t span = (o.length != 0) ? o.length : type_width(o.type);
        if (span == 0) return false;
        if (std::size_t{o.offset} + span > sizeof(ConfigSnapshot)) return false;
        if (o.type == OptionType::Str && static_cast<std::size_t>(o.max) > o.length) return false;
        if (o.type == OptionType::StrArr &&
            static_cast<std::size_t>(o.min) * static_cast<std::size_t>(o.max) != o.length)
            return false;
    }
    return true;
}
static_assert(rows_fit(kSchema), "a schema row addresses memory outside ConfigSnapshot");
static_assert(rows_fit(kExtSchema), "an ext row addresses memory outside ConfigSnapshot");

consteval bool ext_rows_are_scalar() {
    for (const Option& o : kExtSchema) {
        if (type_width(o.type) == 0) return false;
    }
    return true;
}
static_assert(ext_rows_are_scalar(), "an ext row must be a fixed-width scalar type");

constexpr std::size_t kIniLineSize = 1024;
constexpr char kSectionStart = '[';
constexpr char kSectionEnd = ']';
constexpr char kInclSection = '+';
constexpr char kCommentChar = ';';
constexpr char kLineEnd = '\n';

constexpr bool is_space(char c) { return c == ' ' || c == '\t'; }

constexpr unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}

constexpr int icmp_n(std::string_view a, std::string_view b, std::size_t n) {
    for (std::size_t k = 0; k < n; ++k) {
        const unsigned char ca = k < a.size() ? lower_ascii(static_cast<unsigned char>(a[k])) : 0u;
        const unsigned char cb = k < b.size() ? lower_ascii(static_cast<unsigned char>(b[k])) : 0u;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
    return 0;
}

constexpr bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && icmp_n(a, b, a.size()) == 0;
}

consteval bool ext_names_are_new() {
    for (const Option& e : kExtSchema) {
        for (const Option& o : kSchema) {
            if (iequals(e.name, o.name)) return false;
        }
    }
    return true;
}
static_assert(ext_names_are_new(), "an ext row reuses a classic option name");

int icmp_full(std::string_view a, std::string_view b) {
    return icmp_n(a, b, std::max(a.size(), b.size()) + 1);
}

class MsgBuf {
public:
    void put(std::string_view s) {
        for (const char ch : s) {
            if (len_ + 1 >= kParseErrorChars) return;
            buf_[len_++] = ch;
        }
    }
    const char* data() const { return buf_; }
    std::size_t size() const { return len_; }

private:
    char buf_[kParseErrorChars]{};
    std::size_t len_ = 0;
};

void record(ConfigSnapshot& into, ParseError::Kind kind, const MsgBuf& msg) {

    if (into.parse_error_count >= kMaxParseErrors) return;
    ParseError& slot = into.parse_errors[into.parse_error_count];
    slot.kind = kind;
    (void)slot.text.assign(std::string_view{msg.data(), msg.size()});
    ++into.parse_error_count;
}

void record_unknown(ConfigSnapshot& into, std::string_view key) {
    MsgBuf m;
    m.put(key);
    m.put(": unknown option");
    record(into, ParseError::Kind::UnknownOption, m);
}

void record_value(ConfigSnapshot& into, ParseError::Kind kind, std::string_view name,
                  std::string_view value, std::string_view what) {
    MsgBuf m;
    m.put(name);
    m.put(": '");
    m.put(value);
    m.put("' ");
    m.put(what);
    record(into, kind, m);
}

std::byte* field_ptr(ConfigSnapshot& s, std::uint16_t off) {
    return reinterpret_cast<std::byte*>(&s) + off;
}

template <class T>
void store_as(std::byte* out, T v) {
    std::memcpy(out, &v, sizeof(T));
}

std::uint32_t load_u32(const std::byte* base, std::size_t index) {
    std::uint32_t v = 0;
    std::memcpy(&v, base + index * sizeof(std::uint32_t), sizeof(v));
    return v;
}

void store_u32(std::byte* base, std::size_t index, std::uint32_t v) {
    std::memcpy(base + index * sizeof(std::uint32_t), &v, sizeof(v));
}

bool has_hex_prefix(const char* text) {
    return text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
}

void parse_numeric(const Option& o, const char* text, std::byte* out, ConfigSnapshot& into) {
    std::uint32_t u32 = 0;
    std::int32_t i32 = 0;
    float f32 = 0.0f;

    const char* endptr = "";
    bool out_of_range = true;
    bool invalid_format = false;

    switch (o.type) {
        case OptionType::Hex8:
        case OptionType::Hex16:
        case OptionType::Hex32:
        case OptionType::Hex32Arr:
            invalid_format = !has_hex_prefix(text);
            [[fallthrough]];
        case OptionType::U8:
        case OptionType::U16:
        case OptionType::U32:
        case OptionType::Array: {
            char* end = nullptr;

            const unsigned long long raw = std::strtoull(text, &end, 0);
            endptr = end;
            u32 = static_cast<std::uint32_t>(raw);
            const std::int64_t before = static_cast<std::int64_t>(u32);
            const std::int64_t after = config_parser::clamp(o, before);
            out_of_range = (after != before);
            u32 = static_cast<std::uint32_t>(after);
            break;
        }
        case OptionType::I8:
        case OptionType::I16:
        case OptionType::I32: {
            char* end = nullptr;
            const long long raw = std::strtoll(text, &end, 0);
            endptr = end;
            i32 = static_cast<std::int32_t>(raw);
            const std::int64_t before = i32;
            const std::int64_t after = config_parser::clamp(o, before);
            out_of_range = (after != before);
            i32 = static_cast<std::int32_t>(after);
            break;
        }
        case OptionType::F32: {
            char* end = nullptr;
            f32 = std::strtof(text, &end);
            endptr = end;
            const float lo = static_cast<float>(o.min);
            const float hi = static_cast<float>(o.max);
            if (f32 < lo)
                f32 = lo;
            else if (f32 > hi)
                f32 = hi;
            else
                out_of_range = false;
            break;
        }
        case OptionType::Str:
        case OptionType::StrArr:
            out_of_range = false;
            break;
    }

    if (*endptr != '\0') {
        record_value(into, ParseError::Kind::NotANumber, o.name, text, "not a number");
    } else if (out_of_range) {
        record_value(into, ParseError::Kind::OutOfRange, o.name, text, "out of range");
    } else if (invalid_format) {
        record_value(into, ParseError::Kind::InvalidFormat, o.name, text, "invalid format");
    }

    switch (o.type) {
        case OptionType::Hex8:
        case OptionType::U8:
            store_as<std::uint8_t>(out, static_cast<std::uint8_t>(u32));
            break;
        case OptionType::I8:
            store_as<std::int8_t>(out, static_cast<std::int8_t>(i32));
            break;
        case OptionType::Hex16:
        case OptionType::U16:
            store_as<std::uint16_t>(out, static_cast<std::uint16_t>(u32));
            break;
        case OptionType::I16:
            store_as<std::int16_t>(out, static_cast<std::int16_t>(i32));
            break;
        case OptionType::Hex32:
        case OptionType::Hex32Arr:
        case OptionType::Array:
        case OptionType::U32:
            store_as<std::uint32_t>(out, u32);
            break;
        case OptionType::I32:
            store_as<std::int32_t>(out, i32);
            break;
        case OptionType::F32:
            store_as<float>(out, f32);
            break;
        case OptionType::Str:
        case OptionType::StrArr:
            break;
    }
}

struct SectionVerdict {
    SectionMatch kind = SectionMatch::NoMatch;
    bool matched = false;
    bool video_header = false;
};

SectionVerdict classify_section(std::string_view raw, const config_parser::PassNames& names) {
    SectionVerdict v;
    if (raw.empty()) return v;
    const char lead = raw.front();
    if (lead != kSectionStart && lead != kInclSection) return v;

    const std::string_view body = raw.substr(1);
    std::size_t end = body.size();
    std::size_t wc_pos = std::string_view::npos;
    std::size_t eq_pos = std::string_view::npos;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char ch = body[i];
        if (ch == kSectionEnd) {
            end = i;
            break;
        }
        if (ch == '*') wc_pos = i;
        if (ch == '=') eq_pos = i;
    }
    const std::string_view name = body.substr(0, end);

    const std::string_view cur = names.core_name;
    const std::string_view orig =
        names.orig_core_name.empty() ? names.core_name : names.orig_core_name;

    if (iequals(name, "MiSTer") || (names.is_arcade && iequals(name, "arcade")) ||
        (names.arcade_vertical && iequals(name, "arcade_vertical"))) {
        v.kind = SectionMatch::ExactCoreName;
        v.matched = true;
        return v;
    }

    const bool wildcard = (wc_pos != std::string_view::npos);
    const bool hit = wildcard ? (icmp_n(name, orig, wc_pos) == 0 || icmp_n(name, cur, wc_pos) == 0)
                              : (iequals(name, orig) || iequals(name, cur));
    if (hit) {
        v.kind = wildcard ? SectionMatch::PrefixWildcard : SectionMatch::ExactCoreName;
        v.matched = true;
        return v;
    }

    if (eq_pos != std::string_view::npos && icmp_n(name, "video", eq_pos) == 0) {
        v.video_header = true;
        const std::string_view mode = name.substr(eq_pos + 1);
        if (iequals(mode, names.video_qualified)) {

            v.kind = (names.video_qualified == names.video_unqualified)
                         ? SectionMatch::VideoUnqualified
                         : SectionMatch::VideoQualified;
            v.matched = true;
        }
    }
    return v;
}

bool read_line(std::string_view text, std::size_t& pos, std::string& line) {
    line.clear();
    char c = 0;
    bool ignore = false;
    bool skip = true;
    for (;;) {
        c = (pos < text.size()) ? text[pos++] : '\0';
        if (c == '\0') break;
        if (!is_space(c)) skip = false;

        if (line.size() >= kIniLineSize - 1 || c == kCommentChar) ignore = true;
        if (c == kLineEnd) break;
        if (config_parser::admits(c) && !ignore && !skip) line.push_back(c);
    }

    while (!line.empty() && is_space(line.back()))
        line.pop_back();
    return c == '\0';
}

char char_at(const std::string& s, std::size_t i) { return i < s.size() ? s[i] : '\0'; }

void parse_var(const std::string& line, ConfigSnapshot& into,
               bool (&array_append)[kNumIniOptions + kNumExtOptions]) {

    std::size_t i = 0;
    for (;;) {
        const char ch = char_at(line, i);
        if (ch == '=' || is_space(ch)) break;

        if (ch == '\0') return;
        ++i;
    }
    const std::string_view key{line.data(), i};

    ++i;
    while (char_at(line, i) == '=' || is_space(char_at(line, i)))
        ++i;

    const char* value = line.c_str() + i;

    constexpr std::size_t kNoRow = kNumIniOptions + kNumExtOptions;
    std::size_t var_id = kNoRow;
    for (std::size_t j = 0; j < std::size(kSchema); ++j) {
        if (iequals(key, kSchema[j].name)) var_id = j;
    }

    for (std::size_t j = 0; var_id == kNoRow && j < std::size(kExtSchema); ++j) {
        if (iequals(key, kExtSchema[j].name)) var_id = kNumIniOptions + j;
    }
    if (var_id == kNoRow) {
        record_unknown(into, key);
        return;
    }

    const Option& o =
        (var_id < kNumIniOptions) ? kSchema[var_id] : kExtSchema[var_id - kNumIniOptions];
    std::byte* base = field_ptr(into, o.offset);
    const std::size_t vlen = std::strlen(value);

    switch (o.type) {
        case OptionType::Str: {

            const std::size_t cap = static_cast<std::size_t>(o.max);
            std::memset(base, 0, cap);
            const std::size_t n = std::min(vlen, cap - 1);
            std::memcpy(base, value, n);
            break;
        }
        case OptionType::StrArr: {

            const std::size_t slots = static_cast<std::size_t>(o.min);
            const std::size_t item = static_cast<std::size_t>(o.max);
            if (!array_append[var_id]) {
                array_append[var_id] = true;
                for (std::size_t n = 0; n < slots; ++n) {
                    base[n * item] = std::byte{0};
                }
            }
            for (std::size_t n = 0; n < slots; ++n) {
                std::byte* slot = base + n * item;
                if (slot[0] != std::byte{0}) continue;
                std::memset(slot, 0, item);
                std::memcpy(slot, value, std::min(vlen, item - 1));
                break;
            }

            break;
        }
        case OptionType::Array:
        case OptionType::Hex32Arr: {

            const std::size_t capacity = o.length / sizeof(std::uint32_t);
            if (!array_append[var_id]) {
                array_append[var_id] = true;
                store_u32(base, 0, 0);
            }
            const std::uint32_t count = load_u32(base, 0);

            if (static_cast<std::size_t>(count) + 1 < capacity) {
                const std::uint32_t pos = count + 1;
                store_u32(base, 0, pos);
                parse_numeric(o, value, base + pos * sizeof(std::uint32_t), into);
            }
            break;
        }
        case OptionType::U8:
        case OptionType::U16:
        case OptionType::U32:
        case OptionType::I32:
        case OptionType::I8:
        case OptionType::I16:
        case OptionType::Hex8:
        case OptionType::Hex16:
        case OptionType::Hex32:
        case OptionType::F32:

            parse_numeric(o, value, base, into);
            break;
    }
}

}  // namespace

std::span<const Option> option_schema() { return kSchema; }

std::span<const Option> option_schema_ext() { return kExtSchema; }

bool config_parser::admits(char c) {

    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '[' ||
           c == ']' || c == '(' || c == ')' || c == '-' || c == '+' || c == '/' || c == '=' ||
           c == '#' || c == '$' || c == '@' || c == '_' || c == ',' || c == '.' || c == '!' ||
           c == '*' || c == ':' || c == '~' || c == ' ' || c == '\t';
}

std::int64_t config_parser::clamp(const Option& o, std::int64_t v) {

    if (v < o.min) return o.min;
    if (v > o.max) return o.max;
    return v;
}

SectionMatch config_parser::match_section(std::string_view header, const PassNames& names) {
    if (header.empty()) return SectionMatch::Global;
    return classify_section(header, names).kind;
}

Ex<config_parser::PassOutcome> config_parser::parse(std::string_view text, const PassNames& names,
                                                    ConfigSnapshot& into) {

    PassOutcome outcome;

    bool section = false;

    bool array_append[kNumIniOptions + kNumExtOptions] = {};

    std::string line;
    line.reserve(kIniLineSize);
    std::size_t pos = 0;

    for (;;) {
        const bool eof = read_line(text, pos, line);
        const char first = line.empty() ? '\0' : line.front();

        if (first == kSectionStart) {

            const SectionVerdict v = classify_section(line, names);
            if (v.video_header) outcome.saw_video_section = true;
            if (v.kind == SectionMatch::VideoQualified ||
                v.kind == SectionMatch::VideoUnqualified) {
                outcome.used_video_section = true;
            }
            section = v.matched;
            if (section) {
                for (bool& a : array_append)
                    a = false;
            }
        } else if (first == kInclSection && !section) {

            const SectionVerdict v = classify_section(line, names);
            if (v.video_header) outcome.saw_video_section = true;
            if (v.kind == SectionMatch::VideoQualified ||
                v.kind == SectionMatch::VideoUnqualified) {
                outcome.used_video_section = true;
            }
            section = v.matched;
            if (section) {
                for (bool& a : array_append)
                    a = false;
            }
        } else if (section) {
            parse_var(line, into, array_append);
        }

        if (eof) break;
    }
    return outcome;
}

Ex<ConfigSnapshot> config_parser::parse_two_pass(std::string_view text, const PassNames& names) {
    ConfigSnapshot cfg{};

    auto p1 = parse(text, names, cfg);
    if (!p1) return std::unexpected(p1.error());

    if (p1->saw_video_section && !p1->used_video_section) {
        PassNames unqualified = names;
        unqualified.video_qualified = names.video_unqualified;
        auto p2 = parse(text, unqualified, cfg);
        if (!p2) return std::unexpected(p2.error());
        cfg.second_pass_ran = true;
    }
    return cfg;
}

Ex<void> AltIniIndex::scan_once(std::span<const std::string> root_dirents) {
    if (scanned_) return {};
    scanned_ = true;

    std::size_t found = 0;
    for (const std::string& d : root_dirents) {
        if (found >= kMaxAltInis) break;

        if (icmp_n(d, "MiSTer_", 7) != 0) continue;
        if (!iequals(std::string_view{d}.substr(d.size() - 4), ".ini")) continue;
        names_[found] = d;

        if (names_[found].size() > 63) names_[found].resize(63);
        ++found;
    }

    for (std::size_t i = 1; i < kMaxAltInis; ++i) {
        for (std::size_t j = 1; j < kMaxAltInis; ++j) {
            const bool prev_empty = names_[j - 1].empty();
            const bool cur_empty = names_[j].empty();
            const bool swap_them =
                (prev_empty && !cur_empty) ||
                (!prev_empty && !cur_empty && icmp_full(names_[j - 1], names_[j]) > 0);
            if (swap_them) names_[j - 1].swap(names_[j]);
        }
    }
    return {};
}

std::string_view AltIniIndex::name_for(std::uint8_t alt) const {
    if (alt == 0 || alt >= 4) return "MiSTer.ini";
    return names_[alt - 1];
}

std::string AltIniIndex::label_for(std::uint8_t alt) const {

    if (alt == 0) return "Main";
    const std::string_view name = name_for(alt);
    if (name.empty()) return " -- ";

    char label[6] = {};
    const std::string_view tail = name.substr(7);
    const std::size_t n = std::min<std::size_t>(tail.size(), 5);
    for (std::size_t k = 0; k < n; ++k)
        label[k] = tail[k];

    for (std::size_t k = n; k-- > 0;) {
        if (label[k] == '.') {
            label[k] = '\0';
            break;
        }
    }

    const std::string_view stem{label};
    if (iequals(stem, "alt")) return "Alt1";
    if (iequals(stem, "alt_1")) return "Alt1";
    if (iequals(stem, "alt_2")) return "Alt2";
    if (iequals(stem, "alt_3")) return "Alt3";

    for (std::size_t k = 0; k < 4; ++k) {
        if (label[k] == '\0') label[k] = ' ';
    }
    label[4] = '\0';
    return std::string{label};
}

}  // namespace mister::svc
