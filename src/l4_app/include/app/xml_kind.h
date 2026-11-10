// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/ascii.h"

namespace mister::app {

enum class XmlKind : std::uint32_t { Rbf = 0, Mra = 1, Mgl = 2 };

inline constexpr std::string_view kXmlExtension = ".mra";

[[nodiscard]] constexpr XmlKind xml_kind_of(std::string_view s) noexcept {
    if (s.size() <= kXmlExtension.size())
        return XmlKind::Rbf;
    const std::string_view tail = s.substr(s.size() - kXmlExtension.size());
    if (infra::ieq(tail, ".mra"))
        return XmlKind::Mra;
    if (infra::ieq(tail, ".mgl"))
        return XmlKind::Mgl;
    return XmlKind::Rbf;
}

}
