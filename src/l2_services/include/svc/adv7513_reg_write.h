// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::svc {
struct Modeline;
}

namespace mister::svc::adv7513 {

struct InitOptions;

inline constexpr std::uint8_t kRegPower = 0x41;
inline constexpr std::uint8_t kRegStatus = 0x42;
inline constexpr std::uint8_t kRegIntEnable = 0x94;
inline constexpr std::uint8_t kRegIntStatus = 0x96;
inline constexpr std::uint8_t kRegEdidSegment = 0xC4;
inline constexpr std::uint8_t kRegEdidTrigger = 0xC9;

struct RegWrite {
    std::uint8_t reg = 0;
    std::uint8_t val = 0;
    friend constexpr bool operator==(RegWrite, RegWrite) = default;
};

inline constexpr std::size_t kInitTableSize = 51;
inline constexpr std::size_t kAudioTableSize = 13;

std::array<RegWrite, kInitTableSize> init_table(const InitOptions& o);

std::array<RegWrite, kAudioTableSize> audio_table(const InitOptions& o);

std::array<RegWrite, 3> mode_table(const svc::Modeline& m, bool direct_video_menu);

RegWrite power(bool on);

}  // namespace mister::svc::adv7513
