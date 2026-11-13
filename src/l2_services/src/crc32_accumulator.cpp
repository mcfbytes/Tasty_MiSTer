// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/crc32_accumulator.h"

namespace mister::svc {

template <class Owner>
Crc32 Crc32AccumulatorT<Owner>::value() const noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    return crc_;
}

template class Crc32AccumulatorT<seat_owner::Main>;
template class Crc32AccumulatorT<seat_owner::Encode>;
template class Crc32AccumulatorT<seat_owner::Diag>;

}
