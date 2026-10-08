// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "app/input_decode.h"
#include "app/input_wire.h"
#include "hal/spi_transport.h"
#include "hal/boards_table.h"
#include "proto/core_session.h"
#include "proto/link_router.h"
#include "proto/spi_ps2_decoder.h"
#include "svc/input_emitter.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class InputEmit {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr SeatTag kSeat = SeatTag::RT;

    struct Counts {
        std::uint32_t rt_rounds = 0;
        std::uint32_t joy_transactions = 0;
        std::uint32_t rt_errors = 0;
        std::uint32_t edge_resets = 0;
    };

    InputEmit(InputWire& wire, hal::ISpiTransport& link, proto::ILinkRouter& router) noexcept
        : wire_(wire), ps2_(link, router) {}
    InputEmit(const InputEmit&) = delete;
    InputEmit& operator=(const InputEmit&) = delete;

    void on_rt_round(bool tick, std::uint32_t core_edge_seq, bool live);

    void apply_edge_reset();

    svc::InputEmitter& emitter() noexcept { return em_; }
    const svc::InputEmitter& emitter() const noexcept { return em_; }
    Counts counts() const noexcept;

private:
    svc::InputEmitter em_{};

    InputWire& wire_;

    proto::SpiPs2Decoder ps2_;

    std::uint32_t wm_reset_seq_ = 0;

    std::uint32_t wm_core_edge_seq_ = 0;

    std::atomic<std::uint32_t> n_rt_rounds_{0};
    std::atomic<std::uint32_t> n_joy_tx_{0};
    std::atomic<std::uint32_t> n_edge_reset_{0};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::highest_fifo_seat(m) == InputEmit::kSeat;
              }),
              "item: this lane has no thread of its own. Its body is step "
              "(3) of app::RtMain::round, whose serve() runs on T-RT — "
              "the ONE row holding the strictly highest FIFO priority "
              "(INVARIANT 1), so this join rejects every other seat. Seating "
              "it elsewhere would mean the emitter runs on a thread that is "
              "not the executive's, and every SPI write input makes would be "
              "a SECOND writer of the GPO shadow word hal::SpiBus exists "
              "to solely own (a CORRECTNESS requirement, not tuning).");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputEmit::kSeat).policy == hal::SchedPolicy::Fifo &&
                         hal::seat_of(m, InputEmit::kSeat).prio >
                             hal::seat_of(m, InputDecode::kSeat).prio;
              }),
              "item: this lane DRAINS what the decode lane publishes, on "
              "the round the executive owns. At or below the decode lane's "
              "priority a full 2048-slot key ring could be refilled faster "
              "than it is drained, and the kKeyDrainBudget back-pressure "
              "arm would never make progress (arch §2).");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputEmit::kSeat).cpu !=
                         hal::seat_of(m, InputDecode::kSeat).cpu;
              }),
              "item: the producer and the consumer of InputWire sit on "
              "DIFFERENT cores — that is what makes every cell's wait-free "
              "single-writer discipline worth its cost, and what keeps "
              "device-rate decode work off the RT core (arch's "
              "prefetch_off_rt_cpu neighbourhood).");

}  // namespace mister::app
