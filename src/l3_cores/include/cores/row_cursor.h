// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "cores/boot_asset.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class RowCursor {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit RowCursor(std::span<const BootAsset> rows) noexcept : all_(rows), rest_(rows) {}

    void rewind() noexcept { rest_ = all_; }

    void advance() noexcept {
        if (!rest_.empty())
            rest_ = rest_.subspan(1);
    }

    [[nodiscard]] infra::OptRef<const BootAsset> current() const noexcept {
        return rest_.empty() ? infra::OptRef<const BootAsset>{}
                             : infra::OptRef<const BootAsset>{rest_.front()};
    }

    [[nodiscard]] infra::OptRef<const BootAsset> current_in(BootGroup g) const noexcept {
        const auto row = current();
        return row && row->group == g ? row : infra::OptRef<const BootAsset>{};
    }

    void skip_group(BootGroup g) noexcept {
        while (current_in(g))
            advance();
    }

    void skip_to_group(BootGroup g) noexcept {
        while (const auto row = current()) {
            if (row->group == g)
                return;
            advance();
        }
    }

private:
    std::span<const BootAsset> all_;
    std::span<const BootAsset> rest_;
};

}
