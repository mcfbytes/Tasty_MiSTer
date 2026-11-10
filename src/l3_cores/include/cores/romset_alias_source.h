// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/browse_dirs.h"
#include "cores/entry_lookup.h"
#include "cores/romset_alias.h"

namespace mister::cores {

class IRomsetFiles;

class IRomsetAliasSource {
public:
    virtual ~IRomsetAliasSource() = default;

    [[nodiscard]] virtual RomsetAlias query(
        IRomsetFiles& files,
        BrowseDirs dirs,
        EntryLookup entry
    ) = 0;

protected:
    IRomsetAliasSource() = default;
    IRomsetAliasSource(const IRomsetAliasSource&) = default;
    IRomsetAliasSource& operator=(const IRomsetAliasSource&) = default;
};

}
