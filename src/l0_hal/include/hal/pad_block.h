// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "hal/reg_access.h"
#include "hal/uio_reg_file.h"

namespace mister::hal {

struct PadBlock {
    using Reg = UioRegFile::Reg;
    static constexpr std::size_t kSize = UioRegFile::kSize;
    static constexpr RegAccess kAccess = RegAccess::WriteOnly;
};

}
