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
