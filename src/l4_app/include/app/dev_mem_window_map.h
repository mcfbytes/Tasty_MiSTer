// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/load_window_map.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"
#include "infra/seat_owner_ui.h"

namespace mister::app {

template <class Owner>
class DevMemWindowMap final : public ILoadWindowMap<Owner> {
    TASTY_SEAT_FROM(Owner);

public:
    [[nodiscard]] Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) override;
};

extern template class DevMemWindowMap<seat_owner::Main>;
extern template class DevMemWindowMap<seat_owner::Io>;
extern template class DevMemWindowMap<seat_owner::Ui>;

}
