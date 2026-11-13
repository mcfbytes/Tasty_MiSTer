// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_view.h"
#include "infra/seat.h"
#include "infra/seat_owner_diag.h"
#include "infra/seat_owner_encode.h"
#include "infra/seat_owner_main.h"
#include "svc/types.h"

namespace mister::svc {

template <class Owner>
class Crc32AccumulatorT final {
    TASTY_SEAT_FROM(Owner);

public:
    void extend(std::span<const std::uint8_t> data) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        crc_ = crc32_extend(crc_, data);
    }

    void extend(std::span<const std::byte> data) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        extend(infra::byte_cast<const std::uint8_t>(data));
    }

    [[nodiscard]] Crc32 value() const noexcept;

    void restart() noexcept { crc_ = {}; }

private:
    Crc32 crc_{};
};

extern template class Crc32AccumulatorT<seat_owner::Main>;
extern template class Crc32AccumulatorT<seat_owner::Encode>;
extern template class Crc32AccumulatorT<seat_owner::Diag>;

}
