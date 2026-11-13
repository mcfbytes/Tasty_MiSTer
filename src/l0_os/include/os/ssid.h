// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class Ssid {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxBytes = 32;
    static constexpr std::size_t kHexDigitsPerByte = 2;
    static constexpr std::size_t kMaxHexChars = kHexDigitsPerByte * kMaxBytes;
    static constexpr std::size_t kMaxDisplayBytes = 4 * kMaxBytes;

    [[nodiscard]] static Ex<Ssid> parse(std::string_view octets) noexcept;

    [[nodiscard]] static Ex<Ssid> from_hex(std::string_view hex) noexcept;

    [[nodiscard]] static Ex<Ssid> from_escaped(std::string_view escaped) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return length_; }

    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::span<const std::byte>{octets_.data(), length_};
    }

    class Hex {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {digits_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return digits_.data(); }

    private:
        friend class Ssid;
        std::array<char, kMaxHexChars + 1> digits_{};
        std::size_t length_ = 0;
    };

    class Display {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class Ssid;
        std::array<char, kMaxDisplayBytes + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Hex to_hex() const noexcept;
    [[nodiscard]] Display display() const noexcept;

    friend constexpr bool operator==(const Ssid& a, const Ssid& b) noexcept {
        return a.length_ == b.length_ && a.octets_ == b.octets_;
    }

private:
    std::array<std::byte, kMaxBytes> octets_{};
    std::uint8_t length_ = 0;
};

static_assert(std::is_trivially_copyable_v<Ssid>);
static_assert(std::has_unique_object_representations_v<Ssid>);
static_assert(sizeof(Ssid) == Ssid::kMaxBytes + 1);

}
