// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/mra_facts.h"
#include "infra/error.h"
#include "infra/telemetry.h"
#include "proto/conf_switches.h"
#include "svc/config_snapshot.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

using ConfigCell = xthread::Telemetry<svc::ConfigSnapshot>;

[[nodiscard]] Ex<svc::ConfigSnapshot> parse_ini_for_core(const svc::Vfs* vfs,
                                                         const svc::ConfigSnapshot& defaults,
                                                         std::string_view conf_str_name,
                                                         const MraFacts& facts);

[[nodiscard]] proto::ConfSwitches conf_switches(const svc::ConfigSnapshot& cfg) noexcept;

[[nodiscard]] proto::ConfSwitches default_conf_switches() noexcept;

}  // namespace mister::app
