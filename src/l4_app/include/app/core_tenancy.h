// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
#include "infra/log_lane.h"
#include "infra/log_rec.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/block_slots.h"
#include "proto/core_session.h"
#include "proto/link_op.h"
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
        EmitSite advise_site;
        const os::IClock& clock;
        proto::BlockSlots& slots;
        SessionTally& session_tally;
        xthread::LogLane& log_lane;
        infra::OptRef<svc::IStorageLifecycle> storage_life{};
        infra::OptRef<MailboxRelay> mailbox{};
        infra::OptRef<DiskRelay> disks{};

        infra::OptRef<svc::DiscReadService> discs{};
    };

    explicit CoreTenancy(const Wiring& g) noexcept
        : bay_(g.bay),
          releases_(g.releases),
          binder_(g.binder),
          session_(g.session),
          bindings_(g.bindings),
          machine_(g.machine),
          advise_site_(g.advise_site),
          clock_(g.clock),
          slots_(g.slots),
          session_tally_(g.session_tally),
          log_lane_(g.log_lane.producer()),
          storage_life_(g.storage_life),
          mailbox_(g.mailbox),
          disks_(g.disks),
          discs_(g.discs) {}

    CoreTenancy(const CoreTenancy&) = delete;
    CoreTenancy& operator=(const CoreTenancy&) = delete;

    [[nodiscard]] infra::OptRef<cores::Core> core() const noexcept { return current_; }

    [[nodiscard]] std::uint8_t adopted_slot() const noexcept { return adopted_slot_; }

    [[nodiscard]] bool retiring() const noexcept { return retiring_.gen != 0; }

    [[nodiscard]] bool drop_owed() const noexcept { return drop_owed_; }

    [[nodiscard]] std::uint16_t poisoned_slots() const noexcept {
        std::uint16_t m = 0;
        for (const Retiree& a : abandoned_)
            m = static_cast<std::uint16_t>(m | a.mask);
        return m;
    }

    [[nodiscard]] CoreBay& bay() noexcept { return bay_; }

    [[nodiscard]] const BayReleasesCell& releases() const noexcept { return releases_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    [[nodiscard]] Ex<void> adopt(const proto::LinkOp::AdoptCore& a) {
        TASTY_SEAT_BODY(CoreTenancy);
        using Built = proto::LinkOp::Built;

        if (drop_owed_)
            settle_retire(RetireEnd::Adopt);
        if (mailbox_)
            mailbox_->forget();
        if (disks_)
            disks_->forget();
        switch (a.outcome) {
            case Built::Core:
                break;
            case Built::NoName:
                return {};
            case Built::NoStorage:

                machine_.close(
                    Event::ConfStrOnlySession{.cause = Event::ConfStrOnlyCause::NoStorage},
                    EmitSite{ERR_SITE()}
                );
                return {};
            case Built::RegistryMiss:
                machine_.close(
                    Event::ConfStrOnlySession{.cause = Event::ConfStrOnlyCause::RegistryMiss},
                    EmitSite{ERR_SITE()}
                );
                return {};
            case Built::Refused:
                return std::unexpected(Error{a.err, ERR_SITE(), 0});
        }

        if (a.slot >= kCoreBaySlots || bay_.at(a.slot).gen != a.gen || !bay_.at(a.slot).core) {
            ++tally_.stale_adopts;
            release_(a.slot, a.gen);
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), a.slot});
        }

        if (current_) {

            if (a.slot != adopted_slot_)
                release_(a.slot, a.gen);
            return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
        }
        BuiltCore& built = bay_.at(a.slot);
        current_ = infra::OptRef<cores::Core>{*built.core};
        adopted_slot_ = a.slot;
        adopted_gen_ = a.gen;

        if (built.fed_ring.has_value())
            (void)binder_.adopt_fed_ring(std::move(*built.fed_ring));
        const cores::CoreAdopt adopting{
            .session = session_,
            .declares_turbo = bindings_.declares_turbo()
        };
        if (auto r = current_->adopt(adopting); !r) {
            binder_.release_feeder();
            drop_borrow_();
            return r;
        }
        return {};
    }

    void drop(std::uint64_t t0, std::uint16_t channel_borrows) {
        TASTY_SEAT_BODY(CoreTenancy);
        if (current_.has_value()) {

            if (auto r = current_->shutdown(); !r) {
                machine_.advise(Event::SessionAdvisory{.why = r.error().code}, advise_site_);
            }

            current_->retire();
            ++tally_.core_shutdowns;

            if (retiring_.gen != 0)
                abandon_retiring_(false);
            retiring_ = Retiree{
                .slot = adopted_slot_,
                .gen = adopted_gen_,
                .seat = storage_life_,
                .mask = storage_life_ ? channel_borrows : std::uint16_t{0},
                .sent = 0
            };
            current_.reset();
            adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
            adopted_gen_ = 0;
            retire_due_ns_ = clock().now().count() + kCoreRetireBoundNs;
            retire_t0_ = t0;
            retire_logs_ = true;
        }
        drop_owed_ = true;
    }

    void settle_retire(RetireEnd end) noexcept {
        TASTY_SEAT_BODY(CoreTenancy);
        const bool polling = end == RetireEnd::Poll || end == RetireEnd::Round;
        if (retiring_.gen != 0) {
            const bool home = poll_retiree_(retiring_);
            const bool expired = clock().now().count() >= retire_due_ns_;
            if (home) {
                release_retiree_(retiring_);
                retiring_ = Retiree{};
            } else if (polling && !expired) {
                settle_abandoned_();
                return;
            } else {
                abandon_retiring_(end != RetireEnd::Exit && expired);
            }
        }
        settle_abandoned_();
        if (!drop_owed_)
            return;
        drop_owed_ = false;

        if (discs_)
            discs_->release();

        binder_.release_feeder();
        if (retire_logs_) {
            retire_logs_ = false;
            const LogRec::CoreUnload unloaded{sat_u32((log_now_ns() - retire_t0_) / 1'000'000u)};
            log_lane_.push(unloaded, log_now_ns());
        }
    }

private:
    static constexpr std::int64_t kCoreRetireBoundNs = 2 * proto::BlockSlots::kStagingDeadlineNs;

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    void drop_borrow_() noexcept {
        current_.reset();
        release_(adopted_slot_, adopted_gen_);
        adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
        adopted_gen_ = 0;
    }

    void release_(std::uint8_t slot, std::uint16_t gen) noexcept {
        if (slot >= released_.released_gen.size() || gen == 0)
            return;
        released_.released_gen[slot] = gen;
        releases_.publish(released_);
    }

    [[nodiscard]] bool poll_retiree_(Retiree& r) noexcept {
        for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
            const auto bit = static_cast<std::uint16_t>(1u << i);
            if ((r.mask & bit) == 0u)
                continue;
            const proto::SlotIndex si{i};
            if ((r.sent & bit) == 0u && slots_.submit_detach(si)) {
                r.sent = static_cast<std::uint16_t>(r.sent | bit);
                continue;
            }

            if (!r.seat || r.seat->release_slot(si)) {
                r.mask = static_cast<std::uint16_t>(r.mask & ~bit);
            }
        }
        return r.mask == 0u;
    }

    void abandon_retiring_(bool counted) noexcept {
        if (counted) {
            ++tally_.core_abandons;
            session_tally_.last_error = Error{Errc::timeout, ERR_SITE(), retiring_.mask};
        }
        for (Retiree& a : abandoned_) {
            if (a.gen != 0)
                continue;
            a = retiring_;
            retiring_ = Retiree{};
            return;
        }

        Retiree& into = abandoned_.front();
        into.merge(retiring_);
        retiring_ = Retiree{};
    }

    void release_retiree_(const Retiree& r) noexcept {
        const auto gens = r.bay_gens();
        for (std::size_t i = 0; i < kCoreBaySlots; ++i)
            if (gens[i] != 0)
                release_(static_cast<std::uint8_t>(i), gens[i]);
    }

    void settle_abandoned_() noexcept {
        for (Retiree& a : abandoned_) {
            if (a.gen == 0)
                continue;
            if (a.mask != 0u && !poll_retiree_(a))
                continue;
            release_retiree_(a);
            a = Retiree{};
        }
    }

    CoreBay& bay_;
    BayReleasesCell& releases_;
    BayReleases released_{};
    LinkBinder& binder_;
    proto::CoreSession& session_;
    const SessionBindings& bindings_;
    SessionMachine& machine_;
    const EmitSite advise_site_;
    const os::IClock& clock_;
    proto::BlockSlots& slots_;
    SessionTally& session_tally_;
    xthread::LogLane::Producer log_lane_;
    const infra::OptRef<svc::IStorageLifecycle> storage_life_;
    const infra::OptRef<MailboxRelay> mailbox_;
    const infra::OptRef<DiskRelay> disks_;
    const infra::OptRef<svc::DiscReadService> discs_;

    infra::OptRef<cores::Core> current_{};
    std::uint8_t adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
    std::uint16_t adopted_gen_ = 0;

    Retiree retiring_{};

    std::array<Retiree, kMaxAbandonedCores> abandoned_{};
    std::int64_t retire_due_ns_ = 0;
    std::uint64_t retire_t0_ = 0;
    bool retire_logs_ = false;
    bool drop_owed_ = false;
    Tally tally_{};
};

}
