// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "ui/build_env.h"
#include "ui/options_page.h"
#include "ui/types.h"

namespace mister::ui {

class OptionsModel {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Ex<void> build(const proto::ItemTable& table, const BuildEnv& env);

    std::span<const std::unique_ptr<OptionItem>> items() const noexcept { return items_; }

    RowCount selectable_count() const noexcept { return selectable_; }

    RowCount drawn_count() const noexcept { return drawn_; }

    MenuMask mask() const noexcept;

    std::uint32_t rows_beyond_mask() const noexcept { return beyond_mask_; }

    const OptionItem* at_selectable(RowIndex index) const;

    bool empty_page() const noexcept { return drawn_.v == 0; }

    bool flat() const noexcept { return flat_; }

    std::uint8_t page() const noexcept { return page_; }

private:
    std::vector<std::unique_ptr<OptionItem>> items_;
    RowCount selectable_{};
    RowCount drawn_{};
    std::uint32_t beyond_mask_ = 0;
    std::uint8_t page_ = 0;
    bool flat_ = false;
};

}
