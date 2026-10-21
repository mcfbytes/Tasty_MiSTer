// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::cores::n64 {

class ISaveLayout {
public:
    virtual ~ISaveLayout() = default;

    [[nodiscard]] virtual std::uint32_t save_offset(std::size_t idx) const noexcept = 0;

    [[nodiscard]] virtual std::uint8_t layout_key(std::size_t rows) const noexcept = 0;

protected:
    ISaveLayout() = default;
    ISaveLayout(const ISaveLayout&) = default;
    ISaveLayout& operator=(const ISaveLayout&) = default;
};

}
