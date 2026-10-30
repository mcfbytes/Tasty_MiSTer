// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/addon_send.h"
#include "app/answered.h"
#include "app/belt_orders.h"
#include "app/companion_walk.h"
#include "app/file_tx_pieces.h"
#include "app/load_ladder.h"
#include "app/load_walk.h"
#include "app/manifest_ladder.h"
#include "app/owner_tally.h"
#include "app/picked_load.h"
#include "app/rung.h"
#include "app/rung_edges.h"
#include "app/rung_mask.h"
#include "app/rung_rows.h"
#include "cores/ladder_host.h"
#include "cores/mount_status.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/error.h"

namespace mister::app {

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

class LoadBelt {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Wait : std::uint8_t { None, Later, Now };

    struct SaveStateHold {
        PickedLoad load{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
    };

    class Host {
    public:
        virtual void step_held(RungSlot slot) = 0;
        [[nodiscard]] virtual Wait held_wait(RungSlot slot) const noexcept = 0;

        virtual void content_loaded() = 0;

        virtual void addons_closed() = 0;

        virtual void last_piece_sent(FileTxPieces& pieces) = 0;

        virtual void manifest_failed(Errc why) = 0;
        virtual void manifest_done() = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        virtual ~Host() = default;
    };

    LoadBelt(const RungEdges& edges, OwnerTally& tally) noexcept : edges_(edges), tally_(tally) {}

    LoadBelt(const LoadBelt&) = delete;
    LoadBelt& operator=(const LoadBelt&) = delete;

    [[nodiscard]] bool offer(BeltOrders&& orders);
    void offer(ManifestLadder&& m);
    void offer(CompanionWalk&& c);

    void offer(LoadWalk&& w, std::uint32_t gen);
    void hold(const SaveStateHold& h);

    void refuse_walk(std::uint32_t gen) noexcept;

    void step(Host& host, cores::ILadderHost& ladder);

    void step_companion(Host& host, cores::ILadderHost& ladder);

    [[nodiscard]] Wait wait(const Host& host) const noexcept;

    [[nodiscard]] infra::OptRef<const SaveStateHold> savestate_hold() const noexcept;
    [[nodiscard]] std::optional<SaveStateHold> take_savestate_hold() noexcept;
    [[nodiscard]] bool addons_after_owed() const noexcept;
    void promote_addons_after() noexcept;

    [[nodiscard]] const cores::MountStatus& walk_status() const noexcept { return walk_status_; }

    [[nodiscard]] bool walk_answered() const noexcept { return walk_answered_.has_value(); }

    [[nodiscard]] std::optional<Answered<cores::MountStatus>> take_walk_answer() noexcept;

    void cancel() noexcept;
    void drop_manifest() noexcept;
    void drop_companion() noexcept;

    [[nodiscard]] RungMask live() const noexcept;

    [[nodiscard]] bool any_live(RungMask m) const noexcept { return live().any(m); }

    [[nodiscard]] bool empty() const noexcept { return live().bits() == 0; }

    [[nodiscard]] const std::optional<LoadWalk>& walk() const noexcept { return walk_; }

    [[nodiscard]] const std::optional<CompanionWalk>& companion_walk() const noexcept {
        return companion_walk_;
    }

    [[nodiscard]] const std::optional<ManifestLadder>& manifest() const noexcept {
        return manifest_;
    }

private:
    template <class Self>
    [[nodiscard]] static auto rung_in_(Self& self, RungSlot slot) noexcept;
    [[nodiscard]] infra::OptRef<Rung> rung_(RungSlot slot) noexcept;
    [[nodiscard]] infra::OptRef<const Rung> rung_(RungSlot slot) const noexcept;

    void settle_(RungSlot slot, Rung::Pass pass, Host& host);
    void close_addons_(Rung::Pass pass, Host& host);
    void close_pieces_(Rung::Pass pass, Host& host);
    void close_load_(Rung::Pass pass, Host& host);
    void close_manifest_(Rung::Pass pass, Host& host);
    void close_walk_(Rung::Pass pass, Host& host);

    const RungEdges edges_;
    OwnerTally& tally_;
    std::optional<AddonSend> addons_{};
    std::optional<AddonSend> addons_after_{};
    std::optional<SaveStateHold> ss_hold_{};
    std::optional<FileTxPieces> pieces_{};
    std::optional<LoadLadder> load_{};
    std::optional<ManifestLadder> manifest_{};
    std::optional<LoadWalk> walk_{};
    std::optional<CompanionWalk> companion_walk_{};
    std::uint32_t walk_gen_ = 0;
    cores::MountStatus walk_status_{};
    std::optional<Answered<cores::MountStatus>> walk_answered_{};
};

}
