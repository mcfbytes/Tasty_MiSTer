// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::ui {

struct SourceRevision {
    std::uint32_t v = 0;
    friend constexpr bool operator==(SourceRevision, SourceRevision) = default;
};

}
