// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "cores/nvram_save.h"

namespace mister::cores::mra {

struct NvramDecl {

    std::optional<NvramSave> save{};
    std::size_t after = 0;
};

}
