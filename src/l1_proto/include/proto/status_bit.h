// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::proto {

inline void status_bit_out_of_range() noexcept {}

class StatusBit {
public:
    static constexpr unsigned kCount = 128;

    static constexpr unsigned kWordBits = 16;

    constexpr StatusBit() noexcept = default;

    explicit consteval StatusBit(unsigned n) : StatusBit(Checked{}, n) {
        if (n >= kCount)
            status_bit_out_of_range();
    }

    [[nodiscard]] static constexpr std::optional<StatusBit> of(std::uint64_t n) noexcept {
        return n < kCount ? std::optional<StatusBit>{StatusBit{Checked{}, n}} : std::nullopt;
    }

    [[nodiscard]] constexpr std::uint8_t index() const noexcept { return v_; }

    [[nodiscard]] constexpr std::optional<StatusBit> plus(unsigned n) const noexcept {
        return n < kCount - v_ ? std::optional<StatusBit>{StatusBit{Checked{}, v_ + n}}
                               : std::nullopt;
    }

    [[nodiscard]] constexpr std::size_t word() const noexcept { return v_ / kWordBits; }

    [[nodiscard]] constexpr std::uint16_t mask() const noexcept {
        return static_cast<std::uint16_t>(1u << (v_ % kWordBits));
    }

    friend constexpr bool operator==(StatusBit, StatusBit) = default;
    friend constexpr auto operator<=>(StatusBit, StatusBit) = default;

private:
    struct Checked {};

    constexpr StatusBit(Checked, std::uint64_t n) noexcept : v_(static_cast<std::uint8_t>(n)) {}

    std::uint8_t v_ = 0;
};

enum BitRep : std::uint32_t {};

[[nodiscard]] inline BitRep printed_bit(StatusBit bit) noexcept {
    return static_cast<BitRep>(bit.index());
}

inline constexpr StatusBit kResetBit{0};

}
