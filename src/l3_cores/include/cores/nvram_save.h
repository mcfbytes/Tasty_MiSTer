// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::cores::mra {

struct NvramSave {
    proto::WideIoIndex index{};
    proto::FileSize size{};
    friend constexpr bool operator==(NvramSave, NvramSave) = default;
};

}
