// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/input_decode.h"
#include "app/input_wire.h"
#include "infra/error.h"
#include "hal/boards_table.h"
#include "svc/input_service.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::app {

class InputBuild {
    TASTY_SEAT_RESIDENT(Diag);

public:
    static constexpr hal::Seat kSeat = hal::Seat::Diag;

    struct Counts {
        std::uint32_t enumerate_failures = 0;
        std::uint32_t devices = 0;
        std::uint32_t mapped = 0;
        std::uint32_t slotted = 0;
        std::uint32_t mask_changes = 0;
        std::uint32_t axis_edges = 0;
        std::uint32_t mouse_remainder = 0;
        std::uint32_t key_overflows = 0;
        std::uint32_t quirk_drops = 0;
        std::uint32_t rejected = 0;
        std::uint32_t slot_live = 0;
        std::uint32_t slot_ghosts = 0;
        std::uint32_t slot_hash[svc::kMaxPlayers]{};
    };

    InputBuild(const svc::Vfs& vfs, InputWire& wire) noexcept;
    InputBuild(const InputBuild&) = delete;
    InputBuild& operator=(const InputBuild&) = delete;

    Ex<void> open();

    void set_cfg_deadzone_rules(std::span<const svc::DeadzoneRule> rows);

    void set_input_dir(std::string_view dir);

    bool rebind_round(unsigned wait_ms = 250);

    svc::InputService& service() noexcept { return *svc_; }
    const svc::InputService& service() const noexcept { return *svc_; }

    bool opened() const noexcept { return opened_; }
    Counts counts() const noexcept;

private:
    const svc::Vfs* vfs_;
    InputWire& wire_;

    std::optional<svc::InputService> svc_{};
    std::string input_dir_{};
    bool opened_ = false;

    std::atomic<std::uint32_t> n_enum_fail_{0};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputBuild::kSeat).policy == hal::SchedPolicy::Other;
              }),
              "item: this lane runs opendir(3) over /dev/input, up to "
              "kMaxDevices open(2) calls, EVIOCG* ioctls and the map-file "
              "reads behind load_maps_for, and it nanosleeps for up to "
              "wait_ms inside the item baton. That is the amendment's "
              "REFUSAL of 'move enumerate() onto T-INPUT' made a build error: "
              "on a SCHED_FIFO row it is a directory scan on the RT "
              "partition (arch §2).");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputBuild::kSeat).prio <
                         hal::seat_of(m, InputDecode::kSeat).prio;
              }),
              "item: the baton is a rendezvous in which THIS lane waits "
              "for the decode lane to reach its pause point. The waiter must "
              "be the lower-priority side (input_pipeline.cpp's own note: a "
              "SCHED_OTHER thread waiting on a SCHED_FIFO thread is the safe "
              "direction), or the wait can starve the thread it is waiting "
              "for and every hotplug refuses at the timeout.");
static_assert(InputBuild::kSeat != InputDecode::kSeat,
              "item: build and decode must be TWO seats. On one seat the "
              "baton becomes a self-wait -- rebind_round() would spin for "
              "wait_ms waiting for a pause its own thread cannot publish -- "
              "and enumerate() would rebuild devices_ under the drain() it is "
              "supposed to exclude (row item).");

}  // namespace mister::app
