// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::os {

class Ipv4Addr {
public:
    static constexpr std::size_t kOctets = 4;
    static constexpr std::size_t kMaxTextChars = 15;
    static constexpr std::uint8_t kLinkLocalFirst = 169;
    static constexpr std::uint8_t kLinkLocalSecond = 254;

    [[nodiscard]] static Ex<Ipv4Addr> parse(std::string_view text) noexcept;

    [[nodiscard]] bool is_link_local() const noexcept;

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class Ipv4Addr;
        std::array<char, kMaxTextChars + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Text text() const noexcept;

    friend constexpr bool operator==(const Ipv4Addr&, const Ipv4Addr&) = default;

private:
    std::array<std::uint8_t, kOctets> octets_{};
};

static_assert(std::is_trivially_copyable_v<Ipv4Addr>);
static_assert(std::has_unique_object_representations_v<Ipv4Addr>);
static_assert(sizeof(Ipv4Addr) == Ipv4Addr::kOctets);

}
