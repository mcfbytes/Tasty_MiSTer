// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
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

}
