// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "ui/types.h"

namespace mister::ui {

template <class E>
concept SlotNames = std::is_scoped_enum_v<E> && requires { E::Count; };

template <SlotNames E>
[[nodiscard]] constexpr RowCount named_rows() noexcept {
    return RowCount::of(infra::ordinal(E::Count));
}

class SlotId {
    TASTY_SEAT_EXEMPT(component);

public:
    template <SlotNames E>
    [[nodiscard]] static constexpr SlotId of(E named) noexcept {
        return SlotId{RowIndex::of(infra::ordinal(named))};
    }

    [[nodiscard]] static constexpr SlotId at(RowIndex row) noexcept { return SlotId{row}; }

    template <SlotNames E>
    [[nodiscard]] constexpr bool is(E named) const noexcept {
        return *this == of(named);
    }

    template <SlotNames E>
    [[nodiscard]] constexpr E named() const noexcept {
        return row_ < RowIndex::of(infra::ordinal(E::Count)) ? static_cast<E>(row_.before().size())
                                                             : E::Count;
    }

    [[nodiscard]] constexpr RowIndex row() const noexcept { return row_; }

    friend constexpr bool operator==(SlotId, SlotId) = default;

private:
    constexpr explicit SlotId(RowIndex row) noexcept : row_(row) {}

    RowIndex row_{};
};

}
