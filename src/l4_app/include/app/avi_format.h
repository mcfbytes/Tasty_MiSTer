// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/rec_options.h"

namespace mister::app::avi {

struct Fields {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    RecCodec codec = RecCodec::Cscd;
    std::uint32_t rate = 0;
    std::uint32_t scale = 0;
    std::uint32_t frames = 0;
    std::uint32_t max_chunk = 0;
    std::uint64_t movi_bytes = 0;
    std::uint64_t index_bytes = 0;
};

inline constexpr std::size_t kHeaderBytes = 4096;

inline constexpr std::size_t kMoviFourcc = kHeaderBytes - 4;
inline constexpr std::size_t kChunkHead = 8;
inline constexpr std::size_t kIndexEntry = 16;
inline constexpr std::uint32_t kKeyFlag = 0x10;
inline constexpr std::uint32_t kHasIndex = 0x10;

inline constexpr std::uint32_t kTickHz = 100'000'000;
inline constexpr std::uint32_t kDefaultVtime = 1'666'667;

inline constexpr std::uint32_t kVtimeMin = kTickHz / 120;
inline constexpr std::uint32_t kVtimeMax = kTickHz / 20;
[[nodiscard]] constexpr bool plausible_vtime(std::uint32_t vtime) noexcept {
    return vtime >= kVtimeMin && vtime <= kVtimeMax;
}

using Header = std::array<std::byte, kHeaderBytes>;
void header(const Fields& f, Header& out) noexcept;

void chunk_head(std::uint32_t payload, std::span<std::byte, kChunkHead> out) noexcept;
[[nodiscard]] constexpr std::size_t chunk_bytes(std::size_t payload) noexcept {
    return kChunkHead + payload + (payload & 1u);
}
void index_head(std::uint32_t entries, std::span<std::byte, kChunkHead> out) noexcept;
void index_entry(bool key, std::uint32_t offset, std::uint32_t payload,
                 std::span<std::byte, kIndexEntry> out) noexcept;

}  // namespace mister::app::avi
