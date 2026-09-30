// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "svc/types.h"

namespace mister::svc {

enum class IdSource : std::uint8_t {
    VidPid,
    VidPidUniq,
    VidPidNameSum,
    PhysUniq,
};

struct DeviceIdentity {
    Vid vid{};
    Pid pid{};
    std::uint16_t version = 0;
    std::uint16_t bustype = 0;
    IdSource source = IdSource::VidPid;
    std::string id;

    std::string name;

    std::string merge_id;

    std::uint32_t unique_hash = 0;
    bool unique_filenames = false;

    std::optional<std::uint8_t> port_suffix;
};

}  // namespace mister::svc
