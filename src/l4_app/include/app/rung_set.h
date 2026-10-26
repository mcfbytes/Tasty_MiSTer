// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/addon_send.h"
#include "app/companion_walk.h"
#include "app/file_tx_pieces.h"
#include "app/load_ladder.h"
#include "app/load_walk.h"
#include "app/manifest_ladder.h"
#include "app/picked_load.h"
#include "app/rung.h"
#include "app/rung_mask.h"
#include "app/rung_roster.h"
#include "cores/ladder_host.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

struct RungSet {
    TASTY_SEAT_RESIDENT(Main);

    struct SaveStateHold {
        PickedLoad load{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
    };

    [[nodiscard]] infra::OptRef<Rung> rung(RungSlot slot) noexcept;
    [[nodiscard]] infra::OptRef<const Rung> rung(RungSlot slot) const noexcept;

    [[nodiscard]] RungMask live() const noexcept;

    [[nodiscard]] bool any_live(RungMask m) const noexcept { return live().any(m); }

    std::optional<AddonSend> addons{};
    std::optional<AddonSend> addons_after{};
    std::optional<SaveStateHold> ss_hold{};
    std::optional<FileTxPieces> pieces{};
    std::optional<LoadLadder> load{};
    std::optional<ManifestLadder> manifest{};
    std::optional<LoadWalk> walk{};
    std::optional<CompanionWalk> companion_walk{};
    std::uint32_t walk_gen = 0;
    cores::MountStatus walk_answer{};
    bool walk_answer_fresh = false;

private:
    template <class Self>
    [[nodiscard]] static auto rung_in_(Self& self, RungSlot slot) noexcept;
};

inline constexpr RungMask kLoadBelt{
    RungSlot::Pieces,
    RungSlot::Load,
    RungSlot::Walk,
    RungSlot::Companion,
    RungSlot::Addons,
    RungSlot::AddonsAfter,
    RungSlot::SavestateHold
};
inline constexpr RungMask kContentOwing{
    RungSlot::SavestateHold,
    RungSlot::Pieces,
    RungSlot::Load,
    RungSlot::Walk,
    RungSlot::Companion
};
inline constexpr RungMask
    kWalkBlockers{RungSlot::Walk, RungSlot::Pieces, RungSlot::Load, RungSlot::Addons};
inline constexpr RungMask
    kWatchCutters{RungSlot::Pieces, RungSlot::Load, RungSlot::Walk, RungSlot::Companion};
inline constexpr RungMask kLoaderBusy{RungSlot::Load, RungSlot::Walk, RungSlot::Pieces};

inline constexpr RungMask kSourced{
    RungSlot::Addons,
    RungSlot::AddonsAfter,
    RungSlot::SavestateHold,
    RungSlot::Pieces,
    RungSlot::Load,
    RungSlot::Manifest,
    RungSlot::Walk,
    RungSlot::Companion
};

constexpr bool sourced(RungMask m) noexcept {
    return (m.bits() & ~kSourced.bits()) == 0;
}

static_assert(
    sourced(kLoadBelt)
    && sourced(kContentOwing)
    && sourced(kWalkBlockers)
    && sourced(kWatchCutters)
    && sourced(kLoaderBusy)
);

}
