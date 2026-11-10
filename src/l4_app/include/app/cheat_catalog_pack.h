// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cheat_catalog_cell.h"
#include "cores/cheat_records.h"

namespace mister::app {

[[nodiscard]] CheatCatalog pack_cheat_catalog(const cores::ICheatRecords& records) noexcept;

}
