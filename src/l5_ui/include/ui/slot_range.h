// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "ui/slot_id.h"
#include "ui/types.h"
#include "infra/run.h"

namespace mister::ui {

class SlotRange {
public:
    constexpr SlotRange(RowIndex first, RowCount count) noexcept : run_{first, count} {}

    [[nodiscard]] static constexpr SlotRange table(RowCount count) noexcept {
        return SlotRange{RowIndex{}, count};
    }

    template <SlotNames E>
    [[nodiscard]] static constexpr SlotRange after_named(RowCount count) noexcept {
        return SlotRange{row_after(named_rows<E>()), count};
    }

    [[nodiscard]] static constexpr SlotRange after(SlotRange run, RowCount count) noexcept {
        return SlotRange{run.end(), count};
    }

    [[nodiscard]] constexpr RowIndex end() const noexcept { return run_.end(); }

    [[nodiscard]] constexpr RowCount total() const noexcept { return end().before(); }

    [[nodiscard]] constexpr SlotId first() const noexcept { return SlotId::at(run_.start); }

    [[nodiscard]] constexpr bool contains(SlotId slot) const noexcept {
        return run_.contains(slot.row());
    }

    [[nodiscard]] constexpr std::optional<std::size_t> subscript_of(SlotId slot) const noexcept {
        if (!contains(slot))
            return std::nullopt;
        return (slot.row() - run_.start).size();
    }

private:
    infra::affine::Run<RowIndex> run_{};
};

}
