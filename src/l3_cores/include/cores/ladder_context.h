// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cores/ladder_host.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/reset_edge.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

class BootLadder;
struct CoreProfile;

inline constexpr std::size_t kLadderCarrySlots = 8;

struct LadderContext {
    TASTY_SEAT_EXEMPT(const_shared);
    const svc::Vfs& vfs;
    ILadderHost& host;
    const os::IClock& clock;
    std::string_view image_path{};
    std::string_view last_dir{};
    bool noreset = false;

    bool at_core_start = false;

    bool index0_taken = false;
    proto::IoIndex slot{};

    std::optional<proto::ResetEdge> reset{};
    bool restore = false;
    std::span<const std::string> slot_paths{};
};

using MakeLadder = std::unique_ptr<BootLadder> (*)(const CoreProfile&, const LadderContext&);

}
