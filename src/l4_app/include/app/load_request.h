// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/xml_kind.h"
#include "app/path_text.h"
#include "infra/fixed_str.h"

namespace mister::app {

inline constexpr std::uint16_t kBootGen = 0;

enum class ReloadPolicy : std::uint8_t {
    InProcess,
    ReExec,
};

struct LoadRequest {
    PathText path{};
    PathText xml{};

    XmlKind kind = XmlKind::Rbf;
    ReloadPolicy policy = ReloadPolicy::InProcess;
};

}  // namespace mister::app
