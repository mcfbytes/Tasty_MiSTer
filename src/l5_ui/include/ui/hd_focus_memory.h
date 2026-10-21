// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::ui {

class HdFocusMemory final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kEntries = 16;

    struct Focus {
        RowIndex row{};
        std::optional<RowKey> key{};
    };

    [[nodiscard]] std::optional<Focus> recall(PageId page, ItemId item) const noexcept;

    void remember(PageId page, ItemId item, Focus focus) noexcept;
    void clear() noexcept;

    std::size_t size() const noexcept { return count_; }

    std::uint32_t evictions() const noexcept { return evictions_; }

private:
    struct Entry {
        PageId page = PageId::None;
        ItemId item{};
        Focus focus{};
    };

    std::array<Entry, kEntries> entries_{};
    std::uint8_t count_ = 0;
    std::uint8_t oldest_ = 0;
    std::uint32_t evictions_ = 0;
};

}
