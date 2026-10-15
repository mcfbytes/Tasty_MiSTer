// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/dev_mem_window_map.h"

namespace mister::app {

template <class Owner>
Ex<hal::FpgaMemory> DevMemWindowMap<Owner>::map(const hal::PhysRegion& r) {
    TASTY_SEAT_BODY(SeatOwner);
    return hal::FpgaMemory::map(r);
}

template class DevMemWindowMap<seat_owner::Main>;
template class DevMemWindowMap<seat_owner::Io>;
template class DevMemWindowMap<seat_owner::Ui>;

}  // namespace mister::app
