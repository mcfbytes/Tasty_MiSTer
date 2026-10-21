// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adv7513_reg_write.h"
#include "svc/adv7513_init_options.h"
#include "svc/modeline.h"

namespace mister::svc {

namespace adv7513 {

std::array<RegWrite, kInitTableSize> init_table(const InitOptions& o) {

    const std::uint8_t int0 = o.has_hdmi_int ? 0xC0 : 0x00;

    const std::uint8_t reg56 = 0x08;

    const std::uint8_t reg57 = static_cast<std::uint8_t>(
        (o.hdmi_game_mode ? 0x80u : 0x00u)
        | ((o.ypbpr || o.hdmi_limited) ? 0x04u
               : o.hdr_mode            ? 0x68u
                                       : 0x08u)
    );

    return std::array<RegWrite, kInitTableSize>{{
        {0x98, 0x03},
        {0xD6, 0xC0},
        {0x41, 0x10},
        {0x9A, 0x70},
        {0x9C, 0x30},
        {0x9D, 0x61},
        {0xA2, 0xA4},
        {0xA3, 0xA4},
        {0xE0, 0xD0},
        {0x35, 0x40},
        {0x36, 0xD9},
        {0x37, 0x0A},
        {0x38, 0x00},
        {0x39, 0x2D},
        {0x3A, 0x00},
        {0x16, 0x38},
        {0x17, 0x62},
        {0x3B, 0x80},
        {0x3C, 0x00},
        {0x48, 0x08},
        {0x49, 0xA8},
        {0x40, 0x00},
        {0x4A, 0x80},
        {0x4C, 0x00},
        {0x55, static_cast<std::uint8_t>(o.hdmi_game_mode ? 0x12 : 0x10)},
        {0x56, reg56},
        {0x57, reg57},
        {0x59, static_cast<std::uint8_t>(o.hdmi_game_mode ? 0x30 : 0x00)},
        {0x73, 0x01},
        {0x96, 0xFF},
        {0x94, int0},
        {0xC9, 0x00},
        {0x99, 0x02},
        {0x9B, 0x18},
        {0x9F, 0x00},
        {0xA1, 0x00},
        {0xA4, 0x08},
        {0xA5, 0x04},
        {0xA6, 0x00},
        {0xA7, 0x00},
        {0xA8, 0x00},
        {0xA9, 0x00},
        {0xAA, 0x00},
        {0xAB, 0x40},
        {0xB9, 0x00},
        {0xBA, 0x60},
        {0xBB, 0x00},
        {0xDE, 0x9C},

        {0xE2, 0x01},
        {0xE4, 0x60},
        {0xFA, 0x7D},
    }};
}

std::array<RegWrite, kAudioTableSize> audio_table(const InitOptions& o) {

    return std::array<RegWrite, kAudioTableSize>{{
        {0xAF, static_cast<std::uint8_t>(0x04u | (o.dvi_mode ? 0x00u : 0x02u))},
        {0x0A, 0x00},
        {0x0B, 0x0E},
        {0x0C, 0x04},
        {0x0D, 0x10},
        {0x14, 0x02},
        {0x15, static_cast<std::uint8_t>((o.hdmi_audio_96k ? 0x80u : 0x00u) | 0x20u)},
        {0x01, 0x00},
        {0x02, static_cast<std::uint8_t>(o.hdmi_audio_96k ? 0x30 : 0x18)},
        {0x03, 0x00},
        {0x07, 0x01},
        {0x08, 0x22},
        {0x09, 0x0A},
    }};
}

std::array<RegWrite, 3> mode_table(const Modeline& m, bool direct_video_menu) {

    std::uint8_t pr_flags;
    if (direct_video_menu) {
        pr_flags = 0x00;
    } else if (m.pixel_repeat) {
        pr_flags = 0x48;
    } else {
        pr_flags = 0x40;
    }

    std::uint8_t sync_invert = 0;
    if (!m.hpol)
        sync_invert |= 1u << 5;
    if (!m.vpol)
        sync_invert |= 1u << 6;

    return std::array<RegWrite, 3>{{
        {0x17, static_cast<std::uint8_t>(0x02u | sync_invert)},
        {0x3B, pr_flags},
        {0x3C, m.vic},
    }};
}

RegWrite power(bool on) {

    return RegWrite{kRegPower, static_cast<std::uint8_t>(on ? 0x10 : 0x50)};
}

}

}
