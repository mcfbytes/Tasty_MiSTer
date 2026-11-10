// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mister::svc {

inline constexpr std::size_t kMaxAltInis = 3;

struct AltIniSlot {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<AltIniSlot> of(std::uint8_t alt) noexcept {
        if (alt == 0 || alt > kMaxAltInis)
            return std::nullopt;
        return AltIniSlot{static_cast<std::uint8_t>(alt - 1)};
    }

    friend constexpr bool operator==(AltIniSlot, AltIniSlot) = default;
};

class AltIniIndex {
public:
    AltIniIndex() = default;

    [[nodiscard]] static AltIniIndex scan(std::span<const std::string> root_dirents);

    std::string_view name_for(std::uint8_t alt) const;

    std::string label_for(std::uint8_t alt) const;

private:
    std::array<std::string, kMaxAltInis> names_{};
};

}
