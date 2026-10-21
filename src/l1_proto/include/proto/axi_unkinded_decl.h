// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct AxiRawBase {
    std::uint32_t v = 0;
    friend constexpr bool operator==(AxiRawBase, AxiRawBase) = default;
};

struct AxiUnkindedDecl {
    AxiRawBase value;
    std::uint32_t size;
};

}
