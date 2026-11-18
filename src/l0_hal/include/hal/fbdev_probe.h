// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "os/framebuffer.h"

namespace mister::hal {

struct FbdevProbe {
    std::optional<os::Framebuffer::Window> window{};
    bool drains = false;
};

}
