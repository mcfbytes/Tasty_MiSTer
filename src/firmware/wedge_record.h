// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/heartbeat.h"
#include "infra/json_out.h"
#include "infra/json_width.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "os/kernel_tid.h"
#include "os/task_sched.h"
#include "os/types.h"
#include "run_exit.h"

namespace mister::fw {

enum class WedgeAt : std::uint8_t { Rt, Io, Teardown };

inline constexpr char kWedgeUnknownState = '?';
inline constexpr const char* kWedgeUnknownWord = "?";
inline constexpr const char* kWedgeWithheldWord = "-";

inline constexpr std::string_view kWedgeRecordTag{"wedge"};
inline constexpr std::string_view kWedgeTeardownAt{"teardown"};

inline constexpr infra::JsonWidth kWedgeSeatWorst = infra::JsonWidth::longest(kSeatNames);
inline constexpr infra::JsonWidth
    kWedgeWchanWorst = infra::JsonWidth::of<decltype(os::TaskSched::wchan)>();
inline constexpr infra::JsonWidth kWedgeTeardownAtWorst{kWedgeTeardownAt.size()};

struct WedgeRecord {
    SeatTag seat = SeatTag::Unbound;
    std::optional<os::KernelTid> tid{};
    std::optional<os::TaskSched> sched{};
    std::optional<infra::Heartbeat> heartbeat{};
    std::optional<std::chrono::milliseconds> deadline{};
    std::optional<Error> why{};
    WedgeAt at = WedgeAt::Rt;
    std::optional<ExitCode> exit{};

    [[nodiscard]] constexpr char state_text() const noexcept {
        return sched ? sched->state : kWedgeUnknownState;
    }

    [[nodiscard]] constexpr const char* wchan_text() const noexcept {
        if (!sched)
            return kWedgeUnknownWord;
        return sched->wchan.empty() ? kWedgeWithheldWord : sched->wchan.c_str();
    }
};

constexpr void to_json(infra::JsonOut& o, const WedgeRecord& r) noexcept {
    o.begin_record(infra::JsonName{"wedge"});
    o.str("seat", seat_name(r.seat), kWedgeSeatWorst);

    if (r.tid)
        o.field("tid", *r.tid);
    else
        o.field("tid", 0L);
    if (r.at == WedgeAt::Rt) {
        const char state = r.state_text();
        o.str("state", std::string_view{&state, 1}, infra::JsonWidth{1});
        o.str("wchan", r.wchan_text(), kWedgeWchanWorst);
        os::to_json_sched(o, r.sched);
        if (r.heartbeat)
            o.field("hb", *r.heartbeat);
        if (r.deadline)
            o.field("deadline_ms", r.deadline->count());
        if (r.why) {
            o.field("err", r.why->code);
            o.field("site", r.why->site);
        }
    }
    if (r.at == WedgeAt::Teardown)
        o.str("at", kWedgeTeardownAt, kWedgeTeardownAtWorst);
    if (r.exit)
        o.field("exit", *r.exit);
    o.end_record();
}

}
