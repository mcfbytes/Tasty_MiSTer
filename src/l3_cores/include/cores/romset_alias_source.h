// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/romset_alias.h"

namespace mister::cores {

class IRomsetFiles;

class IRomsetAliasSource {
public:
    virtual ~IRomsetAliasSource() = default;

    [[nodiscard]] virtual RomsetAlias query(IRomsetFiles& files, std::string_view dir,
                                            std::string_view home, std::string_view entry,
                                            std::string_view key) = 0;

protected:
    IRomsetAliasSource() = default;
    IRomsetAliasSource(const IRomsetAliasSource&) = default;
    IRomsetAliasSource& operator=(const IRomsetAliasSource&) = default;
};

}  // namespace mister::cores
