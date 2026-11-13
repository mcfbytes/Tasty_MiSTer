// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/telemetry.h"
#include "svc/ini_catalog.h"

namespace mister::app {

using IniCell = xthread::Telemetry<svc::IniCatalog, SeatTag::Main>;

}
