// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::cores::msu {

inline constexpr std::uint16_t kCdGet = 0x34;
inline constexpr std::uint8_t kCdSet = 0x35;
inline constexpr std::uint8_t kFrameWords = 4;

inline constexpr std::uint16_t kSetEnable = 1;
inline constexpr std::uint16_t kSetTrackSize = 2;
inline constexpr std::uint16_t kSetDataBase = 3;

inline constexpr std::uint16_t kReqNext = 0x34;
inline constexpr std::uint16_t kReqTrack = 0x35;
inline constexpr std::uint16_t kReqSeek = 0x36;
inline constexpr std::uint16_t kReqReset = 0xFF;

inline constexpr std::size_t kSectorBytes = 1024;
inline constexpr std::uint16_t kAudioIndex = 2;
inline constexpr std::uint8_t kDataIndex = 3;

inline constexpr std::uint32_t kGapAt = 0x01A0'0000u;
inline constexpr std::uint32_t kGapBytes = 0x0080'0000u;

inline constexpr std::uint32_t kStockDataBound = 0x1F20'0000u;
inline constexpr std::uint32_t kDataBound = 0x1EA0'0000u;

[[nodiscard]] constexpr std::array<std::uint16_t, 3> set_words(std::uint64_t v) noexcept {
    return {static_cast<std::uint16_t>(v & 0xFFFFu),
            static_cast<std::uint16_t>((v >> 16) & 0xFFFFu),
            static_cast<std::uint16_t>((v >> 32) & 0xFFFFu)};
}
[[nodiscard]] constexpr std::array<std::uint16_t, 3> set_enable(bool present) noexcept {
    return set_words((std::uint64_t{present ? 1u : 0u} << 15) | kSetEnable);
}
[[nodiscard]] constexpr std::array<std::uint16_t, 3> set_track_size(std::uint32_t size) noexcept {
    return set_words((std::uint64_t{size} << 16) | kSetTrackSize);
}
[[nodiscard]] constexpr std::array<std::uint16_t, 3> set_data_base(std::uint32_t base) noexcept {
    return set_words((std::uint64_t{base} << 16) | kSetDataBase);
}

[[nodiscard]] constexpr std::uint64_t window_offset(std::uint64_t file_off) noexcept {
    return file_off < kGapAt ? file_off : file_off + kGapBytes;
}
[[nodiscard]] constexpr std::uint32_t data_extent(std::uint32_t size) noexcept {
    return size > kGapAt ? size + kGapBytes : size;
}

}  // namespace mister::cores::msu
