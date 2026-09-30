// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/option_row.h"
#include "cores/option_row_view.h"
#include "infra/error.h"

namespace mister::cores {

class IOptionRows {
public:
    virtual ~IOptionRows() = default;

    [[nodiscard]] virtual std::span<const OptionRow> option_rows() const noexcept = 0;

    [[nodiscard]] virtual OptionRowView option_view(std::size_t row) noexcept = 0;

    [[nodiscard]] virtual Ex<void> set_option(std::size_t row, std::uint8_t choice) = 0;

    [[nodiscard]] virtual Ex<void> apply_armed_reset() = 0;

protected:
    IOptionRows() = default;
    IOptionRows(const IOptionRows&) = default;
    IOptionRows& operator=(const IOptionRows&) = default;
};

}  // namespace mister::cores
