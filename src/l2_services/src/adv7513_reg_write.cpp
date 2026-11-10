// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adv7513_reg_write.h"
#include "svc/adv7513_bus.h"
#include "svc/adv7513_init_options.h"
#include "svc/modeline.h"

namespace mister::svc {

namespace adv7513 {

namespace {

constexpr std::array<RegWrite, kInitTableSize> kInitTable{{
    {os::I2cReg{0x98}, 0x03},
    {os::I2cReg{0xD6}, 0xC0},
    {kRegPower, 0x10},
    {os::I2cReg{0x9A}, 0x70},
    {os::I2cReg{0x9C}, 0x30},
    {os::I2cReg{0x9D}, 0x61},
    {os::I2cReg{0xA2}, 0xA4},
    {os::I2cReg{0xA3}, 0xA4},
    {os::I2cReg{0xE0}, 0xD0},
    {os::I2cReg{0x35}, 0x40},
    {os::I2cReg{0x36}, 0xD9},
    {os::I2cReg{0x37}, 0x0A},
    {os::I2cReg{0x38}, 0x00},
    {os::I2cReg{0x39}, 0x2D},
    {os::I2cReg{0x3A}, 0x00},
    {os::I2cReg{0x16}, 0x38},
    {kRegVideoFormat, 0x62},
    {kRegPixelRepeat, 0x80},
    {kRegVic, 0x00},
    {os::I2cReg{0x48}, 0x08},
    {os::I2cReg{0x49}, 0xA8},
    {os::I2cReg{0x40}, 0x00},
    {os::I2cReg{0x4A}, 0x80},
    {os::I2cReg{0x4C}, 0x00},
    {kRegGameMode, kGameModeOff},
    {kRegAviAspect, kAviAspectDefault},
    {kRegColorSpace, kColorSpaceDefault},
    {kRegGameModeHdr, 0x00},
    {os::I2cReg{0x73}, 0x01},
    {kRegIntStatus, 0xFF},
    {kRegIntEnable, 0x00},
    {kRegEdidTrigger, 0x00},
    {os::I2cReg{0x99}, 0x02},
    {os::I2cReg{0x9B}, 0x18},
    {os::I2cReg{0x9F}, 0x00},
    {os::I2cReg{0xA1}, 0x00},
    {os::I2cReg{0xA4}, 0x08},
    {os::I2cReg{0xA5}, 0x04},
    {os::I2cReg{0xA6}, 0x00},
    {os::I2cReg{0xA7}, 0x00},
    {os::I2cReg{0xA8}, 0x00},
    {os::I2cReg{0xA9}, 0x00},
    {os::I2cReg{0xAA}, 0x00},
    {os::I2cReg{0xAB}, 0x40},
    {os::I2cReg{0xB9}, 0x00},
    {os::I2cReg{0xBA}, 0x60},
    {os::I2cReg{0xBB}, 0x00},
    {os::I2cReg{0xDE}, 0x9C},

    {os::I2cReg{0xE2}, 0x01},
    {os::I2cReg{0xE4}, 0x60},
    {os::I2cReg{0xFA}, 0x7D},
}};

constexpr std::array<RegWrite, kAudioTableSize> kAudioTable{{
    {kRegAudioMode, kAudioModeHdmi | kAudioModeBit},
    {os::I2cReg{0x0A}, 0x00},
    {os::I2cReg{0x0B}, 0x0E},
    {os::I2cReg{0x0C}, 0x04},
    {os::I2cReg{0x0D}, 0x10},
    {os::I2cReg{0x14}, 0x02},
    {os::I2cReg{0x15}, kAudioI2sStandard},
    {os::I2cReg{0x01}, 0x00},
    {os::I2cReg{0x02}, kAudioN48k},
    {os::I2cReg{0x03}, 0x00},
    {os::I2cReg{0x07}, 0x01},
    {os::I2cReg{0x08}, 0x22},
    {os::I2cReg{0x09}, 0x0A},
}};

consteval bool table_has(const auto& table, os::I2cReg reg) noexcept {
    for (const RegWrite& w : table) {
        if (w.reg == reg)
            return true;
    }
    return false;
}

static_assert(
    table_has(kInitTable, kRegGameMode)
        && table_has(kInitTable, kRegAviAspect)
        && table_has(kInitTable, kRegColorSpace)
        && table_has(kInitTable, kRegGameModeHdr)
        && table_has(kInitTable, kRegIntEnable),
    "every patched init row exists"
);
static_assert(
    table_has(kAudioTable, kRegAudioMode)
        && table_has(kAudioTable, os::I2cReg{0x15})
        && table_has(kAudioTable, os::I2cReg{0x02}),
    "every patched audio row exists"
);

template <std::size_t N>
[[nodiscard]] constexpr std::array<RegWrite, N> patch_row(
    std::array<RegWrite, N> table,
    os::I2cReg reg,
    std::uint8_t val
) {
    for (RegWrite& w : table) {
        if (w.reg == reg)
            w.val = val;
    }
    return table;
}

template <std::size_t N>
[[nodiscard]] constexpr std::array<RegWrite, N> patch_row(
    std::array<RegWrite, N> table,
    RegWrite row
) {
    for (RegWrite& w : table) {
        if (w.reg == row.reg)
            w = row;
    }
    return table;
}

constexpr IntBits int0_value(const InitOptions& o) noexcept {
    return o.has_hdmi_int ? IntBits::hotplug() : IntBits{};
}

constexpr std::uint8_t color_space_value(const InitOptions& o) noexcept {
    if (o.ypbpr || o.hdmi_limited)
        return kColorSpaceYpbPrLimited;
    return o.hdr != HdrMode::Off ? kColorSpaceHdr : kColorSpaceDefault;
}

constexpr std::uint8_t audio_mode_value(const InitOptions& o) noexcept {
    return static_cast<std::uint8_t>(kAudioModeHdmi | (o.dvi_mode ? 0x00 : kAudioModeBit));
}

}

std::array<RegWrite, kInitTableSize> init_table(const InitOptions& o) {
    std::array<RegWrite, kInitTableSize> rows = kInitTable;

    const std::uint8_t reg57 = static_cast<std::uint8_t>(
        (o.hdmi_game_mode ? kColorSpaceGameMode : 0x00u) | color_space_value(o)
    );
    rows = patch_row(
        std::move(rows),
        kRegGameMode,
        static_cast<std::uint8_t>(o.hdmi_game_mode ? kGameModeOn : kGameModeOff)
    );
    rows = patch_row(std::move(rows), kRegColorSpace, reg57);
    rows = patch_row(
        std::move(rows),
        kRegGameModeHdr,
        static_cast<std::uint8_t>(o.hdmi_game_mode ? kGameModeHdrOn : 0x00u)
    );
    rows = patch_row(std::move(rows), int0_value(o).write_row(kRegIntEnable));
    return rows;
}

std::array<RegWrite, kAudioTableSize> audio_table(const InitOptions& o) {
    std::array<RegWrite, kAudioTableSize> rows = kAudioTable;
    rows = patch_row(std::move(rows), kRegAudioMode, audio_mode_value(o));
    rows = patch_row(
        std::move(rows),
        os::I2cReg{0x15},
        static_cast<std::uint8_t>((o.hdmi_audio_96k ? kAudio96kBit : 0x00u) | kAudioI2sStandard)
    );
    rows = patch_row(
        std::move(rows),
        os::I2cReg{0x02},
        static_cast<std::uint8_t>(o.hdmi_audio_96k ? kAudioN96k : kAudioN48k)
    );
    return rows;
}

std::array<RegWrite, 3> mode_table(const Modeline& m, bool direct_video_menu) {

    std::uint8_t pr_flags;
    if (direct_video_menu) {
        pr_flags = kPixelRepeatAuto;
    } else if (m.pixel_repeat) {
        pr_flags = kPixelRepeatManual2x;
    } else {
        pr_flags = kPixelRepeatManual;
    }

    std::uint8_t sync_invert = 0;
    if (!m.hpol)
        sync_invert |= 1u << kHSyncInvertBit;
    if (!m.vpol)
        sync_invert |= 1u << kVSyncInvertBit;

    return std::array<RegWrite, 3>{{
        {kRegVideoFormat, static_cast<std::uint8_t>(kVideoFormatBase | sync_invert)},
        {kRegPixelRepeat, pr_flags},
        {kRegVic, m.vic},
    }};
}

RegWrite power(bool on) {

    constexpr std::uint8_t kPowerOn = 0x10;
    constexpr std::uint8_t kPowerDown = 0x50;
    return RegWrite{kRegPower, static_cast<std::uint8_t>(on ? kPowerOn : kPowerDown)};
}

}

}
