// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::proto {

struct AxiDdrDecl {
    hal::FabricAddr base;
    std::uint32_t size;
};

}
