// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "infra/seat.h"
#include "proto/bit_field.h"
#include "ui/status_view.h"

namespace mister::proto {
class ItemTable;
}

namespace mister::ui {

class SavedBaseline final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void take(const proto::ItemTable& items, const StatusView& live);

    void rebase(const proto::ItemTable& items, const StatusView& live);

    void clear() noexcept;

    [[nodiscard]] bool held() const noexcept;

    [[nodiscard]] bool unsaved(const StatusView& live) const noexcept;

private:
    struct Held {
        StatusView words{};
        std::vector<proto::BitField> fields;
    };

    std::optional<Held> held_;
};

}
