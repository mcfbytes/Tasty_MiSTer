// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/dip_row_view.h"
#include "infra/error.h"

namespace mister::cores {

class IDipSwitches {
public:
    virtual ~IDipSwitches() = default;

    [[nodiscard]] virtual std::size_t dip_count() const noexcept = 0;
    [[nodiscard]] virtual DipRowView dip_row(std::size_t row) const noexcept = 0;
    [[nodiscard]] virtual std::string_view dip_choice(std::size_t row,
                                                      std::size_t choice) const noexcept = 0;

    [[nodiscard]] virtual Ex<void> set_dip(std::size_t row, std::size_t choice) = 0;

    [[nodiscard]] virtual std::string_view dip_file_name() const noexcept = 0;
    [[nodiscard]] virtual std::array<std::uint8_t, 8> dip_bytes() const noexcept = 0;
    [[nodiscard]] virtual bool dips_dirty() const noexcept = 0;
    virtual void mark_dips_saved() noexcept = 0;

protected:
    IDipSwitches() = default;
    IDipSwitches(const IDipSwitches&) = default;
    IDipSwitches& operator=(const IDipSwitches&) = default;
};

}  // namespace mister::cores
