// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/romset_alias.h"
#include "cores/romset_alias_source.h"

namespace mister::app {

using RomsetAlias = cores::RomsetAlias;

using cores::alias_of;
using cores::drops;

using cores::BrowseDirs;
using cores::EntryLookup;

class IRomsetAliases {
public:
    virtual ~IRomsetAliases() = default;

    [[nodiscard]] virtual RomsetAlias query(BrowseDirs dirs, EntryLookup entry) = 0;

protected:
    IRomsetAliases() = default;
    IRomsetAliases(const IRomsetAliases&) = default;
    IRomsetAliases& operator=(const IRomsetAliases&) = default;
};

}
