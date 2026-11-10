// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::cores {

struct EntryLookup {
    std::string_view raw_name;
    std::string_view key;
};

}
