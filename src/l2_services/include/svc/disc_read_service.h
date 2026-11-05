// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "infra/counter.h"
#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "infra/telemetry.h"
#include "svc/disc_counters.h"
#include "svc/disc_geometry.h"
#include "svc/disc_mount_slot.h"
#include "svc/disc_mounter.h"
#include "svc/disc_read_slot.h"
#include "svc/disc_reader.h"
#include "svc/io_coworker.h"

namespace mister::svc {

class DiscReadService final : public IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<DiscReadSlot, kDiscReadDepth, SeatTag::RT, SeatTag::Io>;
    using Loan = Chan::Loan;
    using MountChan = xthread::
        LoanChannel<DiscMountSlot, kDiscMountDepth, SeatTag::RT, SeatTag::Io>;
    using GeomCell = xthread::Telemetry<DiscGeometry, SeatTag::Io>;
    using CountCell = xthread::Telemetry<DiscCounters, SeatTag::Io>;

    struct Wiring {
        GeomCell& geometry;
        CountCell& counters;
        infra::OptRef<IDiscReader> reader{};
        infra::OptRef<IDiscMounter> mounter{};
    };

    explicit DiscReadService(const Wiring& w) noexcept;

    DiscReadService(xthread::WakeFlag& io_wake, const Wiring& w) noexcept;

    [[nodiscard]] GeomCell& geometry_cell() noexcept { return geom_cell_; }

    [[nodiscard]] CountCell& counters_cell() noexcept { return count_cell_; }

    void set_generation(Generation g) noexcept;

    [[nodiscard]] Generation generation() const noexcept { return gen_; }

    [[nodiscard]] DiscMountState arm_mount(
        DiscOp op,
        const CuePolicy* policy,
        std::string_view path
    ) noexcept;

    void release() noexcept;

    [[nodiscard]] DiscMountState mount_state() noexcept;

    [[nodiscard]] const DiscGeometry& geometry() noexcept;
    [[nodiscard]] const DiscCounters& counters() noexcept;

    [[nodiscard]] std::size_t take(
        DiscForm form,
        proto::Lba lba,
        std::span<std::byte> dst
    ) noexcept;

    void hint(DiscForm form, proto::Lba lba) noexcept;

    [[nodiscard]] bool take_read_failed(DiscForm form, proto::Lba lba) noexcept;

    [[nodiscard]] std::uint32_t not_resident() const noexcept { return not_resident_.get(); }

    [[nodiscard]] std::uint32_t evictions() const noexcept { return evictions_.get(); }

    [[nodiscard]] std::uint32_t stale_drops() const noexcept { return stale_drops_.get(); }

    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_.get(); }

    [[nodiscard]] std::uint32_t reads() const noexcept { return reads_.get(); }

    [[nodiscard]] std::uint32_t read_errors() const noexcept { return read_errors_.get(); }

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    void serve_on_io(DiscReadSlot& s) noexcept;

    void pump_on_caller() noexcept;

    [[nodiscard]] Chan& channel_for_test() noexcept { return chan_; }

private:
    void drain_() noexcept;
    [[nodiscard]] bool issue_(DiscForm form, std::uint32_t lba) noexcept;
    [[nodiscard]] bool evict_oldest_resident_() noexcept;
    [[nodiscard]] bool in_flight_(DiscForm form, std::uint32_t lba) const noexcept;
    void serve_(DiscReadSlot& s) noexcept;

    struct Inflight {
        std::uint32_t lba = 0;
        DiscForm form = DiscForm::RawFrame;
        bool live = false;
    };

    struct FailedRead {
        std::uint32_t lba = 0;
        DiscForm form = DiscForm::RawFrame;
        bool live = false;
    };

    void poll_mounts_() noexcept;
    void serve_mounts_io_() noexcept;

    Chan chan_;
    MountChan mounts_;
    GeomCell& geom_cell_;
    CountCell& count_cell_;
    GeomCell::Reader geom_rd_{};
    CountCell::Reader count_rd_{};
    DiscGeometry geom_{};
    DiscCounters count_{};
    infra::OptRef<IDiscReader> reader_;
    infra::OptRef<IDiscMounter> mounter_;

    FixedStr<kDiscPathCap, StrFit::Reject> armed_path_{};
    DiscOp armed_op_ = DiscOp::Mount;
    DiscMountState mount_state_ = DiscMountState::Idle;
    bool mount_live_ = false;

    struct Arrival {
        TASTY_SEAT_RESIDENT(RT);
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Arrival next() const noexcept { return Arrival{v + 1}; }

        [[nodiscard]] constexpr bool before(Arrival other, Arrival now) const noexcept {
            return (now.v - v) > (now.v - other.v);
        }
    };

    class Resident {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] bool held() const noexcept { return static_cast<bool>(loan_); }

        void place(Loan l, Arrival arrival) noexcept {
            loan_ = std::move(l);
            arrival_ = arrival;
        }

        void drop() noexcept {
            loan_ = Loan{};
            arrival_ = Arrival{};
        }

        [[nodiscard]] const DiscReadSlot& read() const noexcept { return *loan_; }

        [[nodiscard]] bool matches(DiscForm form, proto::Lba lba) const noexcept {
            return loan_ && (*loan_).lba == lba && (*loan_).form == form;
        }

        [[nodiscard]] bool older_than(const Resident& other, Arrival now) const noexcept {
            return arrival_.before(other.arrival_, now);
        }

    private:
        Loan loan_{};
        Arrival arrival_{};
    };

    std::array<Resident, kDiscReadDepth> resident_{};
    Arrival arrivals_{};
    Inflight inflight_[kDiscReadDepth]{};
    FailedRead failed_{};
    Generation gen_{};
    xthread::Counter not_resident_, stale_drops_, refusals_, reads_, read_errors_, evictions_;
};

}
