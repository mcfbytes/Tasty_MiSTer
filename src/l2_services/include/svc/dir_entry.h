// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "proto/types.h"

namespace mister::svc {

using proto::FileSize;

struct DirEntry {
    std::string name;
    bool is_dir = false;
    bool is_zip_member = false;
    FileSize size{};
};

}  // namespace mister::svc
