// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/path_text.h"
#include "proto/types.h"

namespace mister::app {

struct StartFile {
    PathText path{};
    proto::WideIoIndex dest{};
    proto::FileOffset at{};
};

}
