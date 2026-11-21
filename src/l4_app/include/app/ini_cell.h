// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/xthread/telemetry.h"
#include "svc/ini_catalog.h"

namespace mister::app {

using IniCell = xthread::Telemetry<svc::IniCatalog, SeatTag::Main>;

}
