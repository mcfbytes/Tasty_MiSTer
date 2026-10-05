// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rt_setup.h"
#include "os/kernel_rt.h"
#include "os/vm_compaction.h"
#include "hal/thread_map.h"
#include "infra/seat.h"

namespace mister::fw {

struct RtEvidence {
    TASTY_SEAT_MEDIATOR(Any, Diag);
    RtSetup mem_lock;
    RtSetup trim_pin;
    RtSetup mmap_pin;

    RtSetup cpu_topology;

    RtSetup cpu_coverage;
    long online_cpus = 0;
    RtSetup main_affinity;
    RtSetup diag_affinity;
    RtSetup ui_affinity;
    RtSetup diag_sched;
    RtSetup ui_sched;
    RtSetup frame_fifo;
    RtSetup frame_affinity;
    RtSetup input_fifo;
    RtSetup input_affinity;
    RtSetup prefetch_fifo;
    RtSetup prefetch_affinity;
    RtSetup pcm_fifo;
    RtSetup pcm_affinity;
    RtSetup io_fifo;
    RtSetup io_affinity;
    RtSetup capture_fifo;
    RtSetup capture_affinity;
    RtSetup encode_sched;
    RtSetup encode_affinity;
    RtSetup recwrite_sched;
    RtSetup recwrite_affinity;

    RtSetup launcher_sched;
    RtSetup launcher_affinity;
    RtSetup rt_fifo;
    RtSetup rt_affinity;

    RtSetup main_stack_prefault;
    std::size_t main_stack_prefault_bytes = 0;
    std::uint64_t stack_rlimit_bytes = 0;

    RtSetup irq_affinity;

    RtSetup kernel_rt;

    os::KernelRt kernel = os::KernelRt::Unknown;

    os::VmCompaction vm_compaction;

    RtSetup seat_name[hal::kThreadSeats];
};

using RtEvidenceRow = RtSetup RtEvidence::*;
inline constexpr std::size_t kRtEvidenceRows = 30;
inline constexpr std::array<RtEvidenceRow, kRtEvidenceRows> kRtEvidenceRowTable{
    &RtEvidence::mem_lock,
    &RtEvidence::trim_pin,
    &RtEvidence::mmap_pin,
    &RtEvidence::cpu_topology,
    &RtEvidence::main_affinity,
    &RtEvidence::diag_affinity,
    &RtEvidence::ui_affinity,
    &RtEvidence::diag_sched,
    &RtEvidence::ui_sched,
    &RtEvidence::frame_fifo,
    &RtEvidence::frame_affinity,
    &RtEvidence::input_fifo,
    &RtEvidence::input_affinity,
    &RtEvidence::prefetch_fifo,
    &RtEvidence::prefetch_affinity,
    &RtEvidence::rt_fifo,
    &RtEvidence::rt_affinity,
    &RtEvidence::kernel_rt,
    &RtEvidence::main_stack_prefault,
    &RtEvidence::irq_affinity,
    &RtEvidence::pcm_fifo,
    &RtEvidence::pcm_affinity,
    &RtEvidence::io_fifo,
    &RtEvidence::io_affinity,
    &RtEvidence::capture_fifo,
    &RtEvidence::capture_affinity,
    &RtEvidence::encode_sched,
    &RtEvidence::encode_affinity,
    &RtEvidence::recwrite_sched,
    &RtEvidence::recwrite_affinity,
};

consteval bool rt_evidence_rows_named_once() {
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if (kRtEvidenceRowTable[i] == nullptr) return false;
        for (std::size_t j = i + 1; j < kRtEvidenceRows; ++j) {
            if (kRtEvidenceRowTable[i] == kRtEvidenceRowTable[j]) return false;
        }
    }
    return true;
}
static_assert(rt_evidence_rows_named_once(),
              "every rt.ok row must name a distinct RtEvidence member — a null slot was "
              "once dereferenced as UB, a duplicate would render one row twice");

static_assert(kRtEvidenceRowTable[17] == &RtEvidence::kernel_rt);
static_assert(kRtEvidenceRowTable[18] == &RtEvidence::main_stack_prefault);
static_assert(kRtEvidenceRowTable[19] == &RtEvidence::irq_affinity);
static_assert(kRtEvidenceRowTable[20] == &RtEvidence::pcm_fifo);
static_assert(kRtEvidenceRowTable[21] == &RtEvidence::pcm_affinity);
static_assert(kRtEvidenceRowTable[22] == &RtEvidence::io_fifo);
static_assert(kRtEvidenceRowTable[23] == &RtEvidence::io_affinity);
static_assert(kRtEvidenceRowTable[24] == &RtEvidence::capture_fifo);
static_assert(kRtEvidenceRowTable[29] == &RtEvidence::recwrite_affinity);

inline constexpr unsigned kRtEvidenceAllApplied = (1u << kRtEvidenceRows) - 1u;

constexpr unsigned rt_evidence_mask(const RtEvidence& ev) noexcept {
    unsigned m = 0;
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if ((ev.*kRtEvidenceRowTable[i]).applied) m |= (1u << i);
    }
    return m;
}

constexpr unsigned rt_first_errno(const RtEvidence& ev) noexcept {
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if (const int e = (ev.*kRtEvidenceRowTable[i]).err; e != 0) return static_cast<unsigned>(e);
    }
    return 0;
}

}  // namespace mister::fw
