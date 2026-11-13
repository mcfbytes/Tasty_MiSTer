// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::os {

class BtAddr {
public:
    static constexpr std::size_t kOctets = 6;
    static constexpr std::size_t kTextChars = 17;
    static constexpr std::size_t kSepChars = kOctets - 1;
    static constexpr std::size_t kCharsPerOctet = 3;
    static constexpr char kSep = ':';
    static constexpr int kNibbleBits = 4;
    static constexpr int kNibbleMask = 0x0F;

    [[nodiscard]] static Ex<BtAddr> parse(std::string_view text) noexcept;

    [[nodiscard]] static Ex<BtAddr> parse(std::string_view text, char separator) noexcept;

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class BtAddr;
        std::array<char, kTextChars + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Text format(char separator = kSep) const noexcept;

    friend constexpr bool operator==(const BtAddr&, const BtAddr&) = default;

private:
    std::array<std::uint8_t, kOctets> octets_{};
};

static_assert(std::is_trivially_copyable_v<BtAddr>);
static_assert(std::has_unique_object_representations_v<BtAddr>);
static_assert(sizeof(BtAddr) == BtAddr::kOctets);

}
