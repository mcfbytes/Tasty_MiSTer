// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/romset_alias.h"

namespace mister::app {

using RomsetAlias = cores::RomsetAlias;

class IRomsetAliases {
public:
    virtual ~IRomsetAliases() = default;

    [[nodiscard]] virtual RomsetAlias query(std::string_view dir, std::string_view home,
                                            std::string_view entry, std::string_view key) = 0;

protected:
    IRomsetAliases() = default;
    IRomsetAliases(const IRomsetAliases&) = default;
    IRomsetAliases& operator=(const IRomsetAliases&) = default;
};

}  // namespace mister::app
