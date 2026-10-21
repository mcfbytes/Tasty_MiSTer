// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace mister::cores::neogeo {

inline constexpr std::size_t kNeoHeaderBytes = 4096;

struct NeoHeader {
    std::array<std::uint8_t, 3> magic{};
    std::uint8_t version = 0;
    std::uint32_t p_size = 0;
    std::uint32_t s_size = 0;
    std::uint32_t m_size = 0;
    std::uint32_t v1_size = 0;
    std::uint32_t v2_size = 0;
    std::uint32_t c_size = 0;
    std::uint32_t year = 0;
    std::uint32_t genre = 0;
    std::uint32_t screenshot = 0;
    std::uint32_t ngh = 0;
    std::array<std::uint8_t, 33> name{};
    std::array<std::uint8_t, 17> manu{};
};

[[nodiscard]] std::optional<NeoHeader> parse_neo_header(std::span<const std::uint8_t> buf) noexcept;

[[nodiscard]] std::string_view name_view(const NeoHeader& hdr) noexcept;

struct NeoQuirk {
    std::uint16_t id = 0;
    std::uint8_t hw = 0;
    std::uint8_t cmc = 0;
    std::uint8_t pvc = 0;
    std::uint8_t sma = 0;
    std::uint8_t xram = 0;
    std::uint8_t mir = 0;
    std::uint8_t rwait = 0;
    std::uint8_t pwait = 0;
};

inline constexpr std::size_t kNeoQuirkCount = 17;

extern const std::array<NeoQuirk, kNeoQuirkCount> kNeoQuirks;

struct NeoCartConfig {
    std::uint32_t hw_type = 0;
    std::uint32_t cmc = 0;
    std::uint32_t pvc = 0;
    std::uint32_t sma = 0;
    std::uint32_t xram = 0;
    bool vrom_mirror = true;
    std::uint32_t rom_wait = 0;
    std::uint32_t p_wait = 0;
    std::uint32_t ms5p = 0;
};

[[nodiscard]] NeoCartConfig resolve_quirks(const NeoHeader& hdr) noexcept;

}
