// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

#include "cores/n64_rom.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"

namespace mister::cores::n64 {

template <class Owner>
class WordReverseT final : public IWordOrder {
    TASTY_SEAT_FROM(Owner);

public:
    constexpr void normalize(std::span<std::uint8_t> data) const noexcept override {
        if (!std::is_constant_evaluated())
            TASTY_SEAT_BODY(SeatOwner);
        detail::reverse_words(data);
    }
};

extern template class WordReverseT<seat_owner::Main>;
extern template class WordReverseT<seat_owner::Io>;

using WordReverse = WordReverseT<seat_owner::Main>;

}
