// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"

namespace mister::svc {

inline constexpr std::size_t kMaxAltInis = 3;

class AltIniIndex {
public:
    Ex<void> scan_once(std::span<const std::string> root_dirents);

    std::string_view name_for(std::uint8_t alt) const;

    std::string label_for(std::uint8_t alt) const;

private:
    bool scanned_ = false;
    std::string names_[kMaxAltInis];
};

}  // namespace mister::svc
