// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/syscall.h>
#include <unistd.h>

#include "infra/decimal_text.h"
#include "infra/json_out.h"

namespace mister::os {

struct KernelTid {
    long v = 0;

    [[nodiscard]] static KernelTid current() noexcept { return KernelTid{::syscall(SYS_gettid)}; }

    [[nodiscard]] bool is_thread_group_leader() const noexcept { return v == ::getpid(); }

    [[nodiscard]] ::pid_t pid() const noexcept { return static_cast<::pid_t>(v); }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend void json_field(infra::JsonOut& o, infra::JsonName key, KernelTid t) {
        o.field(key, t.v);
    }

    friend constexpr bool operator==(KernelTid, KernelTid) = default;
};

[[nodiscard]] inline bool calling_task_leads_process() noexcept {
    return KernelTid::current().is_thread_group_leader();
}

}
