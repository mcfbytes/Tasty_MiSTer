// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/seat.h"
#include "ui/progress_page.h"

namespace mister::ui {

struct PageContext;

class TransferTracker final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void begin(const PageContext& ctx) noexcept;

    [[nodiscard]] std::string_view name() const noexcept {
        return std::string_view(name_, name_len_);
    }

    void clear() noexcept { in_transfer_ = false; }

    [[nodiscard]] bool in_transfer() const noexcept { return in_transfer_; }

private:
    bool in_transfer_ = false;
    std::uint32_t load_asks_seen_ = 0;
    std::uint8_t name_len_ = 0;
    char name_[kProgressNameMax]{};
};

}
