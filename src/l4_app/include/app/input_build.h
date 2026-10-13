// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/device_table_channel.h"
#include "app/input_decode.h"
#include "app/input_wire.h"
#include "hal/boards_table.h"
#include "svc/input_enumerator.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::app {

class InputBuild {
    TASTY_SEAT_RESIDENT(Diag);

public:
    static constexpr SeatTag kSeat = SeatTag::Diag;

    struct Counts {
        std::uint32_t enumerate_failures = 0;
        std::uint32_t devices = 0;
        std::uint32_t rejected = 0;
    };

    InputBuild(const svc::Vfs& vfs, InputWire& wire, std::string_view input_dir = {});
    InputBuild(const InputBuild&) = delete;
    InputBuild& operator=(const InputBuild&) = delete;

    void set_cfg_deadzone_rules(std::span<const svc::DeadzoneRule> rows);

    bool rebind_round();

    svc::InputEnumerator& enumerator() noexcept { return en_; }
    const svc::InputEnumerator& enumerator() const noexcept { return en_; }

    DeviceTableChannel& tables() noexcept { return tables_; }

    void note_watch_failure() noexcept { InputWire::bump(n_enum_fail_, 1); }
    Counts counts() const noexcept;

private:
    bool hand_over_();

    InputWire& wire_;
    svc::InputEnumerator en_;
    DeviceTableChannel tables_{};
    DeviceTableChannel::Sender tx_{tables_};

    std::atomic<std::uint32_t> n_enum_fail_{0};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputBuild::kSeat).policy == hal::SchedPolicy::Other;
              }),
              "this lane runs opendir(3) over /dev/input, up to kMaxDevices open(2) "
              "calls, EVIOCG* ioctls and the map-file reads behind load_maps_for: on a "
              "SCHED_FIFO row that is a directory scan on the RT partition (arch §2), "
              "which is why enumeration never moves onto the decode lane.");
static_assert(InputBuild::kSeat != InputDecode::kSeat,
              "build and decode must be TWO seats: the table is built whole on one and "
              "installed whole on the other, and on one seat the build would run inside "
              "the decode round it is meant to stay out of.");

}  // namespace mister::app
