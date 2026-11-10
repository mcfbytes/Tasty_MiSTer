// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/bay_releases.h"
#include "app/built_core.h"
#include "app/event.h"
#include "app/link_binder.h"
#include "app/log_clock.h"
#include "app/disk_relay.h"
#include "app/mailbox_relay.h"
#include "app/retiree.h"
#include "app/session_bindings.h"
#include "app/session_machine.h"
#include "app/session_tally.h"
#include "app/types.h"
#include "cores/core_adopt.h"
#include "cores/core_support.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/log_lane.h"
#include "infra/log_rec.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/block_slots.h"
#include "proto/core_session.h"
#include "proto/link_op.h"
#include "proto/no_block_roles.h"
#include "proto/types.h"
#include "svc/disc_read_service.h"
#include "svc/storage_lifecycle.h"

namespace mister::app {

class CoreTenancy {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {

        std::uint32_t stale_adopts = 0;

        std::uint32_t core_abandons = 0;

        std::uint32_t core_shutdowns = 0;
    };

    enum class RetireEnd : std::uint8_t { Poll, Round, Adopt, Exit };

    struct Wiring {
        CoreBay& bay;
        BayReleasesCell& releases;
        LinkBinder& binder;
        proto::CoreSession& session;
        const SessionBindings& bindings;
        SessionMachine& machine;
        infra::ErrorSite advise_site;
        const os::IClock& clock;
        proto::BlockSlots& slots;
        SessionTally& session_tally;
        xthread::LogLane& log_lane;
        infra::OptRef<svc::IStorageLifecycle> storage_life{};
        infra::OptRef<MailboxRelay> mailbox{};
        infra::OptRef<DiskRelay> disks{};

        infra::OptRef<svc::DiscReadService> discs{};
    };

    explicit CoreTenancy(const Wiring& g) noexcept;

    CoreTenancy(const CoreTenancy&) = delete;
    CoreTenancy& operator=(const CoreTenancy&) = delete;

    [[nodiscard]] infra::OptRef<cores::Core> core() const noexcept { return current_; }

    [[nodiscard]] proto::CoreBaySlot adopted_slot() const noexcept { return adopted_slot_; }

    [[nodiscard]] bool retiring() const noexcept { return retiring_.gen != 0; }

    [[nodiscard]] bool drop_owed() const noexcept { return drop_owed_; }

    [[nodiscard]] proto::BlockSlotMask poisoned_slots() const noexcept;

    [[nodiscard]] CoreBay& bay() noexcept { return bay_; }

    [[nodiscard]] const BayReleasesCell& releases() const noexcept { return releases_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    [[nodiscard]] Ex<void> adopt(const proto::LinkOp::AdoptCore& a);

    void drop(LogInstant t0, proto::BlockSlotMask channel_borrows);

    void settle_retire(RetireEnd end) noexcept;

private:
    static constexpr std::chrono::nanoseconds kCoreRetireBoundNs{
        2 * proto::BlockSlots::kStagingDeadlineNs
    };

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    void drop_borrow_() noexcept;

    void release_(proto::CoreBaySlot slot, std::uint16_t gen) noexcept;

    [[nodiscard]] bool poll_retiree_(Retiree& r) noexcept;

    void abandon_retiring_(bool counted) noexcept;

    void release_retiree_(const Retiree& r) noexcept;

    void settle_abandoned_() noexcept;

    CoreBay& bay_;
    BayReleasesCell& releases_;
    BayReleases released_{};
    LinkBinder& binder_;
    proto::CoreSession& session_;
    const SessionBindings& bindings_;
    SessionMachine& machine_;
    const infra::ErrorSite advise_site_;
    const os::IClock& clock_;
    proto::BlockSlots& slots_;
    SessionTally& session_tally_;
    xthread::LogLane::Producer log_lane_;
    const infra::OptRef<svc::IStorageLifecycle> storage_life_;
    const infra::OptRef<MailboxRelay> mailbox_;
    const infra::OptRef<DiskRelay> disks_;
    const infra::OptRef<svc::DiscReadService> discs_;

    infra::OptRef<cores::Core> current_{};
    proto::CoreBaySlot adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
    std::uint16_t adopted_gen_ = 0;

    Retiree retiring_{};

    std::array<Retiree, kMaxAbandonedCores> abandoned_{};
    os::Deadline retire_due_ = os::Deadline::immediate();
    LogInstant retire_t0_{};
    bool retire_logs_ = false;
    bool drop_owed_ = false;
    Tally tally_{};
};

}
