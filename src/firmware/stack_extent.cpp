// SPDX-License-Identifier: GPL-3.0-or-later
#include "stack_extent.h"
#include "infra/posix_compat.h"

#include <pthread.h>
#include <sys/resource.h>

#include <bit>
#include <cerrno>
#include <cstdint>

#include "os/kernel_tid.h"
#include "os/stack_touch.h"

namespace mister::fw {

[[nodiscard]] StackPrefault prefault_current_stack(std::size_t requested) noexcept {
    StackPrefault out;
    rlimit rl{};
    if (::getrlimit(RLIMIT_STACK, &rl) != 0) {
        out.outcome = RtSetup{false, errno};
        return out;
    }
    out.rlimit = static_cast<std::uint64_t>(rl.rlim_cur);
    pthread_attr_t attr;
    if (const int rc = ::pthread_getattr_np(::pthread_self(), &attr); rc != 0) {
        out.outcome = RtSetup{false, rc};
        return out;
    }
    void* base = nullptr;
    std::size_t mapped = 0;
    const int grc = ::pthread_attr_getstack(&attr, &base, &mapped);
    (void)::pthread_attr_destroy(&attr);
    if (grc != 0) {
        out.outcome = RtSetup{false, grc};
        return out;
    }
    const auto top = std::bit_cast<std::uintptr_t>(base) + mapped;
    const auto sp = std::bit_cast<std::uintptr_t>(__builtin_frame_address(0));

    const std::size_t used = (sp < top && sp >= top - mapped) ? top - sp : mapped;

    const bool growable = !infra::kPosixGlibc && os::calling_task_leads_process();
    const StackExtent stack{used, mapped, out.rlimit};
    out.bytes = stack.span(requested, growable);
    os::touch_stack_pages(os::StackWalk{.span = out.bytes, .page = os::PageSize{kStackPageBytes}});
    out.outcome = RtSetup{out.bytes == requested, out.bytes == requested ? 0 : ENOMEM};
    return out;
}

}
