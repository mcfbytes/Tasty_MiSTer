// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mister::svc {

inline constexpr std::size_t kMaxAltInis = 3;

class AltIniIndex {
public:
    AltIniIndex() = default;

    [[nodiscard]] static AltIniIndex scan(std::span<const std::string> root_dirents);

    std::string_view name_for(std::uint8_t alt) const;

    std::string label_for(std::uint8_t alt) const;

private:
    std::string names_[kMaxAltInis];
};

}
