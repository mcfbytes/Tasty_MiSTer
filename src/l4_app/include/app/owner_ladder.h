// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "app/content_pick.h"
#include "app/ladder_cell.h"
#include "app/link_tx_channel.h"
#include "app/mount_queue.h"
#include "app/owner_tally.h"
#include "cores/boot_ladder.h"
#include "cores/ladder_host.h"
#include "cores/registry.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/reset_edge.h"

namespace mister::app {

class OwnerLadder final {
    TASTY_SEAT_RESIDENT(Main);

public:
    using MountAsk = MountQueue::MountAsk;
    using Owed = ContentPick::Owed;

    struct Ended {
        bool ok = false;
        bool touched = false;
        std::uint8_t slot = 0;
        Owed owed{};
    };

    struct Step {
        std::optional<Ended> ended{};
        bool bios_missing = false;
    };

    OwnerLadder(
        const LinkTxChannel<SeatTag::Main>& inbox,
        LadderStateCell& cell,
        OwnerTally& tally,
        const svc::Vfs& vfs,
        const os::IClock& clock
    ) noexcept;
    OwnerLadder(const OwnerLadder&) = delete;
    OwnerLadder& operator=(const OwnerLadder&) = delete;

    [[nodiscard]] bool arm(
        const MountAsk& ask,
        const cores::CoreFactory& row,
        cores::ILadderHost& host
    );
    [[nodiscard]] bool arm_start(
        const FixedStr<64, StrFit::Clip>& core_name,
        bool index0_taken,
        cores::ILadderHost& host
    );

    [[nodiscard]] bool arm_core(
        std::optional<proto::ResetEdge> reset,
        std::string_view config,
        const cores::CoreFactory& row,
        cores::ILadderHost& host,
        Owed owed
    );

    [[nodiscard]] Step step();
    void drop();

    [[nodiscard]] bool live() const noexcept;
    [[nodiscard]] bool moved() const noexcept;
    [[nodiscard]] bool start_armed() const noexcept;

    [[nodiscard]] bool noreset() const noexcept;
    void clear_noreset() noexcept;
    void restore_noreset(bool carried) noexcept;

    [[nodiscard]] const LadderStateCell& cell() const noexcept { return cell_; }

private:
    [[nodiscard]] std::optional<Ended> finish_(bool ok);
    void publish_(bool live);

    const LinkTxChannel<SeatTag::Main>& inbox_;
    LadderStateCell& cell_;
    OwnerTally& tally_;
    const svc::Vfs& vfs_;
    const os::IClock& clock_;
    std::unique_ptr<cores::BootLadder> ladder_;
    std::string last_dir_;
    bool noreset_ = false;
    bool moved_ = false;
    bool start_ = false;
    bool core_ = false;

    std::array<std::string, cores::kLadderCarrySlots> slot_paths_{};
    Owed owed_{};
    std::uint8_t slot_ = 0;
};

}
