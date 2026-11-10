// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "os/types.h"

namespace mister::svc {
struct Modeline;
}

namespace mister::svc::adv7513 {

struct InitOptions;

inline constexpr os::I2cReg kRegPower{0x41};
inline constexpr os::I2cReg kRegStatus{0x42};
inline constexpr os::I2cReg kRegIntEnable{0x94};
inline constexpr os::I2cReg kRegIntStatus{0x96};
inline constexpr os::I2cReg kRegEdidSegment{0xC4};
inline constexpr os::I2cReg kRegEdidTrigger{0xC9};

inline constexpr os::I2cReg kRegVideoFormat{0x17};
inline constexpr os::I2cReg kRegPixelRepeat{0x3B};
inline constexpr os::I2cReg kRegVic{0x3C};
inline constexpr os::I2cReg kRegGameMode{0x55};
inline constexpr os::I2cReg kRegAviAspect{0x56};
inline constexpr os::I2cReg kRegColorSpace{0x57};
inline constexpr os::I2cReg kRegGameModeHdr{0x59};
inline constexpr os::I2cReg kRegAudioMode{0xAF};

inline constexpr std::uint8_t kAviAspectDefault = 0x08;
inline constexpr std::uint8_t kGameModeOff = 0x10;
inline constexpr std::uint8_t kGameModeOn = 0x12;
inline constexpr std::uint8_t kGameModeHdrOn = 0x30;
inline constexpr std::uint8_t kColorSpaceDefault = 0x08;
inline constexpr std::uint8_t kColorSpaceYpbPrLimited = 0x04;
inline constexpr std::uint8_t kColorSpaceHdr = 0x68;
inline constexpr std::uint8_t kColorSpaceGameMode = 0x80;
inline constexpr std::uint8_t kAudioModeHdmi = 0x04;
inline constexpr std::uint8_t kAudioModeBit = 0x02;

inline constexpr std::uint8_t kAudio96kBit = 0x80;
inline constexpr std::uint8_t kAudioI2sStandard = 0x20;
inline constexpr std::uint8_t kAudioN96k = 0x30;
inline constexpr std::uint8_t kAudioN48k = 0x18;

inline constexpr std::uint8_t kPixelRepeatAuto = 0x00;
inline constexpr std::uint8_t kPixelRepeatManual = 0x40;
inline constexpr std::uint8_t kPixelRepeatManual2x = 0x48;
inline constexpr std::uint8_t kVideoFormatBase = 0x02;
inline constexpr unsigned kHSyncInvertBit = 5;
inline constexpr unsigned kVSyncInvertBit = 6;

struct RegWrite {
    os::I2cReg reg{};
    std::uint8_t val = 0;
    friend constexpr bool operator==(RegWrite, RegWrite) = default;
};

inline constexpr std::size_t kInitTableSize = 51;
inline constexpr std::size_t kAudioTableSize = 13;

std::array<RegWrite, kInitTableSize> init_table(const InitOptions& o);

std::array<RegWrite, kAudioTableSize> audio_table(const InitOptions& o);

std::array<RegWrite, 3> mode_table(const svc::Modeline& m, bool direct_video_menu);

RegWrite power(bool on);

}
