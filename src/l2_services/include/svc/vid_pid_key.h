// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

#include "svc/types.h"

namespace mister::svc {

struct VidPidKey {
    std::uint32_t v = 0;
    static constexpr unsigned kVidShift = 16;

    [[nodiscard]] static constexpr VidPidKey of(Vid vid, Pid pid) noexcept {
        return VidPidKey{
            (static_cast<std::uint32_t>(vid.v) << kVidShift) | static_cast<std::uint32_t>(pid.v)
        };
    }

    [[nodiscard]] static std::optional<VidPidKey> parse_hex(std::string_view text) noexcept {
        constexpr std::size_t kDigits = 8;
        constexpr std::size_t kPrefix = 2;
        constexpr int kRadix = 16;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(kPrefix);
        if (text.empty() || text.size() > kDigits)
            return std::nullopt;
        std::uint32_t value = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), value, kRadix);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            return std::nullopt;
        return VidPidKey{value};
    }

    [[nodiscard]] static constexpr VidPidKey every_device() noexcept { return VidPidKey{1}; }

    friend constexpr bool operator==(VidPidKey, VidPidKey) = default;
};

static_assert(sizeof(VidPidKey) == sizeof(std::uint32_t), "the packed key is one word");
static_assert(
    VidPidKey::of(Vid{0x054C}, Pid{0x05C4}) == VidPidKey{0x054C05C4u},
    "vid occupies the high half"
);

}
