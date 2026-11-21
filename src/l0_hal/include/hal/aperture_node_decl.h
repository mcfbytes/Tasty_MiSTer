// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "os/types.h"

namespace mister::hal {

struct ApertureNodeDecl {
    std::string_view uio_name;
    os::PhysAddr base{};
};

}
