// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/json_out.h"
#include "os/kernel_tid.h"
#include "os/types.h"

namespace mister::os {

struct RtPriority {
    int v = 0;

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        RtPriority p
    ) noexcept {
        o.field(key, p.v);
    }

    friend constexpr bool operator==(RtPriority, RtPriority) = default;
    friend constexpr auto operator<=>(RtPriority, RtPriority) = default;
};

struct TaskSched {
    SchedPolicy policy = SchedPolicy::Other;
    RtPriority rt_priority{};
    char state = '?';
    FixedStr<31, StrFit::Clip> wchan;
};

constexpr void to_json_sched(infra::JsonOut& o, const std::optional<TaskSched>& sched) noexcept {
    if (!sched) {

        constexpr int kUnread = -1;
        o.field("policy", kUnread);
        o.field("prio", kUnread);
        return;
    }
    o.field("policy", sched->policy);
    o.field("prio", sched->rt_priority);
}

[[nodiscard]] Ex<TaskSched> read_task_sched(KernelTid tid);

}
