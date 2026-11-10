// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::os {

class MmioRegion;

struct MapLength {
    std::size_t v = 0;

    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    void release(const volatile void* base) const noexcept;

    [[nodiscard]] std::span<std::byte> plain_bytes(void* base) const noexcept;
    friend constexpr bool operator==(MapLength, MapLength) = default;
    friend constexpr auto operator<=>(MapLength, MapLength) = default;

private:
    friend class MmioRegion;

    [[nodiscard]] void* map_shared(int prot, int fd, std::int64_t file_offset) const noexcept;
};

}
