// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/n64_save_layout_key.h"
#include "cores/save_row_count.h"

namespace mister::cores::n64 {

class ILayoutKeySource {
public:
    virtual ~ILayoutKeySource() = default;

    [[nodiscard]] virtual SaveLayoutKey layout_key(SaveRowCount rows) const noexcept = 0;

protected:
    ILayoutKeySource() = default;
    ILayoutKeySource(const ILayoutKeySource&) = default;
    ILayoutKeySource& operator=(const ILayoutKeySource&) = default;
};

}
