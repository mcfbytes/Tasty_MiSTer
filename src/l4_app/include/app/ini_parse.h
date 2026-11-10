// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "app/mra_facts.h"
#include "infra/error.h"
#include "infra/telemetry.h"
#include "proto/conf_switches.h"
#include "svc/config_snapshot.h"

namespace mister::svc {
class Vfs;
class IFile;
}

namespace mister::app {

inline constexpr std::uint64_t kIniCap = 1u << 20;

[[nodiscard]] Ex<std::vector<std::byte>> read_ini_whole(const svc::Vfs& vfs);
[[nodiscard]] Ex<std::vector<std::byte>> read_ini_whole(svc::IFile& file);

using ConfigCell = xthread::Telemetry<svc::ConfigSnapshot, SeatTag::Main>;

[[nodiscard]] svc::ConfigSnapshot parse_ini_for_core(
    const svc::Vfs* vfs,
    const svc::ConfigSnapshot& defaults,
    std::string_view conf_str_name,
    const MraFacts& facts
);

[[nodiscard]] proto::ConfSwitches conf_switches(const svc::ConfigSnapshot& cfg) noexcept;

[[nodiscard]] proto::ConfSwitches default_conf_switches() noexcept;

}
