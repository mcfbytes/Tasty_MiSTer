// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "cores/n64_save_layout_key.h"

namespace mister::cores::n64 {

class ISaveLayout {
public:
    virtual ~ISaveLayout() = default;

    [[nodiscard]] virtual SaveLayoutKey layout_key(std::size_t rows) const noexcept = 0;

protected:
    ISaveLayout() = default;
    ISaveLayout(const ISaveLayout&) = default;
    ISaveLayout& operator=(const ISaveLayout&) = default;
};

}
