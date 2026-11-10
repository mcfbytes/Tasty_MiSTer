// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

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

    struct MountArm {
        proto::IoIndex slot{};
        std::string_view image{};
    };

    struct CoreStartArm {

        bool index0_taken = false;
    };

    struct ResetArm {
        proto::ResetEdge edge;
    };

    struct RestoreArm {
        std::string_view config;
    };

    enum class ArmKind : std::uint8_t { Mount, CoreStart, Reset, Restore, kCount };
    using Arm = std::variant<MountArm, CoreStartArm, ResetArm, RestoreArm>;
    static_assert(
        std::variant_size_v<Arm> == static_cast<std::size_t>(ArmKind::kCount),
        "LadderContext: one ArmKind name per arm"
    );
    template <ArmKind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Arm>, T>;
    static_assert(
        kArmIs<ArmKind::Mount, MountArm>
        && kArmIs<ArmKind::CoreStart, CoreStartArm>
        && kArmIs<ArmKind::Reset, ResetArm>
        && kArmIs<ArmKind::Restore, RestoreArm>
    );

    const svc::Vfs& vfs;
    ILadderHost& host;
    const os::IClock& clock;
    std::string_view last_dir{};
    bool noreset = false;
    std::span<const std::string> slot_paths{};
    Arm arm = MountArm{};

    [[nodiscard]] ArmKind kind() const noexcept { return static_cast<ArmKind>(arm.index()); }
};

using MakeLadder = std::unique_ptr<BootLadder> (*)(const CoreProfile&, const LadderContext&);

}
