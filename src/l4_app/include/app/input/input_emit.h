// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/pad_relay_tally.h"
#include "app/link_rx_channel.h"
#include "app/pad_relay_cell.h"
#include "app/pad_route.h"
#include "app/types.h"
#include "hal/spi_transport.h"
#include "hal/boards_table.h"
#include "proto/link/core_session.h"
#include "proto/link_router.h"
#include "infra/prelude.h"
#include "proto/link/spi_ps2_decoder.h"
#include "svc/input/input_emitter.h"
#include "infra/tally.h"
#include "infra/xthread/counter.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link/pad_relay.h"
#include "infra/xthread/telemetry.h"
#include "infra/xthread/wake_flag.h"
#include "os/clock.h"

namespace mister::app::input {

struct InputEmitCounts {
    std::uint32_t rt_rounds = 0;
    std::uint32_t rt_errors = 0;
    std::uint32_t edge_resets = 0;
};

struct PadRelayEnds {
    infra::OptRef<LinkRxChannel<SeatTag::Input>> input_rx{};
    infra::OptRef<const PadRouteCell> route{};
};

class InputEmit {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr SeatTag kSeat = SeatTag::RT;

    using Counts = InputEmitCounts;

    using CountsCell = xthread::Telemetry<Counts, SeatTag::RT>;

    InputEmit(
        const xthread::WakeFlag& edge_reset,
        hal::ISpiTransport& link,
        proto::ILinkRouter& router,
        PadRelayEnds relay = {}
    ) noexcept
        : edge_reset_(edge_reset),
          ps2_(link, router),
          relay_ends_(relay) {}

    InputEmit(const InputEmit&) = delete;
    InputEmit& operator=(const InputEmit&) = delete;

    void on_rt_round(bool tick, CoreEdgeSeq core_edge_seq, bool live);

    void apply_edge_reset();

    void relay(bool gated) noexcept;

    svc::input::InputEmitter& emitter() noexcept { return em_; }

    const svc::input::InputEmitter& emitter() const noexcept { return em_; }

    Counts counts() const noexcept;

    const CountsCell& counts_cell() const noexcept { return counts_cell_; }

    [[nodiscard]] const PadRelayCell& relay_cell() const noexcept { return relay_cell_; }

    [[nodiscard]] const PadRelayTally& relay_tally() const noexcept { return relay_tally_; }

private:
    void answer_relay_() noexcept;

    svc::input::InputEmitter em_{};

    const xthread::WakeFlag& edge_reset_;

    proto::link::SpiPs2Decoder ps2_;

    xthread::WakeFlag::Cursor reset_seen_{};

    CoreEdgeSeq wm_core_edge_seq_{};

    PadRelayCell relay_cell_{};
    PadRelayEnds relay_ends_;

    struct RelayOwed {
        infra::Tally seq{};
        bool gated = false;
        os::MonoInstant at{};
    };

    std::optional<RelayOwed> relay_owed_{};
    PadRelayTally relay_tally_{};

    CountsCell counts_cell_{};
    xthread::Counter n_rt_rounds_{};
    xthread::Counter n_edge_reset_{};
};

static_assert(
    hal::highest_fifo_seat(hal::kSeatPolicyTable) == InputEmit::kSeat,
    "item: this lane has no thread of its own. Its body is step "
    "(3) of app::RtMain::round, whose serve() runs on T-RT — "
    "the ONE row holding the strictly highest FIFO priority "
    "(INVARIANT 1), so this join rejects every other seat. Seating "
    "it elsewhere would mean the emitter runs on a thread that is "
    "not the executive's, and every SPI write input makes would be "
    "a SECOND writer of the GPO shadow word hal::SpiBus exists "
    "to solely own (a CORRECTNESS requirement, not tuning)."
);
static_assert(
    hal::seat_of(hal::kSeatPolicyTable, InputEmit::kSeat).policy == hal::SchedPolicy::Fifo
        && hal::seat_of(hal::kSeatPolicyTable, InputEmit::kSeat).prio
            > hal::seat_of(hal::kSeatPolicyTable, SeatTag::Input).prio,
    "item: this lane DRAINS what the decode lane publishes, on "
    "the round the executive owns. At or below the decode lane's "
    "priority the decode lane could refill what this lane drains "
    "faster than it is drained."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, InputEmit::kSeat).cpu != hal::seat_of(m, SeatTag::Input).cpu;
    }),
    "item: the producer and the consumer of the input crossings sit on "
    "DIFFERENT cores — that is what makes every cell's wait-free "
    "single-writer discipline worth its cost, and what keeps "
    "device-rate decode work off the RT core (see rt_cpu_reserved)."
);

}
