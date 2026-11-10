// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/n64_db_tags.h"

namespace mister::cores::n64 {

struct DbMatch {
    std::optional<DbTags> tags;
};

}
