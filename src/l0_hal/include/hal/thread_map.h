// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/seat.h"

namespace mister::hal {

enum class Seat : std::uint8_t {
    RT,
    Frame,
    Input,
    Pcm,
    Prefetch,
    Diag,
    Ui,
    Io,
    Capture,
    Encode,
    RecWrite,
};

enum class SchedPolicy : std::uint8_t { Fifo, Other };

enum class SpawnKind : std::uint8_t { PromoteInPlace, Create };

struct ThreadRole {
    Seat seat;
    const char* name;
    int cpu;
    SchedPolicy policy;
    int prio;
    std::size_t stack_bytes;
    SpawnKind spawn;
};

inline constexpr std::size_t kThreadSeats = 11;
using ThreadMap = std::array<ThreadRole, kThreadSeats>;

inline constexpr std::size_t kCommNameMax = 15;

inline constexpr std::size_t kFifoStackBytes = 256u * 1024u;

inline constexpr std::size_t kOtherStackBytes = 8u * 1024u * 1024u;

inline constexpr std::size_t kRecorderStackBytes = 256u * 1024u;

inline constexpr ThreadMap kDe10ThreadMap{{
    {.seat = Seat::RT,
     .name = "T-RT",
     .cpu = 1,
     .policy = SchedPolicy::Fifo,
     .prio = 40,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Frame,
     .name = "T-FRAME",
     .cpu = 1,
     .policy = SchedPolicy::Fifo,
     .prio = 35,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Input,
     .name = "T-INPUT",
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 25,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Pcm,
     .name = "T-PCM",
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 22,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Prefetch,
     .name = "T-PREFETCH",
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 20,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Diag,
     .name = "T-DIAG",
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Ui,
     .name = "T-UI",
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Io,
     .name = "T-IO",
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 23,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Capture,
     .name = "T-CAPTURE",
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 15,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::Encode,
     .name = "T-ENCODE",
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = Seat::RecWrite,
     .name = "T-RECWRITE",
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
}};

constexpr const ThreadRole& seat_of(const ThreadMap& m, Seat s) noexcept {
    return m[static_cast<std::size_t>(s)];
}

constexpr std::size_t name_len(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

constexpr bool well_formed(const ThreadMap& m) noexcept {
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (static_cast<std::size_t>(m[i].seat) != i) return false;
    }
    return true;
}

constexpr bool rt_holds_highest_fifo_prio(const ThreadMap& m) noexcept {
    const ThreadRole& rt = seat_of(m, Seat::RT);
    if (rt.policy != SchedPolicy::Fifo) return false;
    for (const ThreadRole& r : m) {
        if (r.seat == Seat::RT) continue;
        if (r.policy == SchedPolicy::Fifo && r.prio >= rt.prio) return false;
    }
    return true;
}

constexpr bool prefetch_off_rt_cpu(const ThreadMap& m) noexcept {
    return seat_of(m, Seat::Prefetch).cpu != seat_of(m, Seat::RT).cpu;
}

constexpr bool input_outranks_prefetch(const ThreadMap& m) noexcept {
    const ThreadRole& in = seat_of(m, Seat::Input);
    const ThreadRole& pf = seat_of(m, Seat::Prefetch);
    if (in.policy != SchedPolicy::Fifo || pf.policy != SchedPolicy::Fifo) {
        return false;
    }
    return in.prio > pf.prio;
}

constexpr bool pcm_off_rt_cpu(const ThreadMap& m) noexcept {
    return seat_of(m, Seat::Pcm).cpu != seat_of(m, Seat::RT).cpu;
}

constexpr bool input_outranks_pcm(const ThreadMap& m) noexcept {
    const ThreadRole& in = seat_of(m, Seat::Input);
    const ThreadRole& pcm = seat_of(m, Seat::Pcm);
    if (in.policy != SchedPolicy::Fifo || pcm.policy != SchedPolicy::Fifo) {
        return false;
    }
    return in.prio > pcm.prio;
}

constexpr bool pcm_outranks_prefetch(const ThreadMap& m) noexcept {
    const ThreadRole& pcm = seat_of(m, Seat::Pcm);
    const ThreadRole& pf = seat_of(m, Seat::Prefetch);
    if (pcm.policy != SchedPolicy::Fifo || pf.policy != SchedPolicy::Fifo) {
        return false;
    }
    return pcm.prio > pf.prio;
}

constexpr bool io_off_rt_cpu(const ThreadMap& m) noexcept {
    return seat_of(m, Seat::Io).cpu != seat_of(m, Seat::RT).cpu;
}

constexpr bool input_outranks_io(const ThreadMap& m) noexcept {
    const ThreadRole& in = seat_of(m, Seat::Input);
    const ThreadRole& io = seat_of(m, Seat::Io);
    if (in.policy != SchedPolicy::Fifo || io.policy != SchedPolicy::Fifo) {
        return false;
    }
    return in.prio > io.prio;
}

constexpr bool io_outranks_pcm(const ThreadMap& m) noexcept {
    const ThreadRole& io = seat_of(m, Seat::Io);
    const ThreadRole& pcm = seat_of(m, Seat::Pcm);
    if (io.policy != SchedPolicy::Fifo || pcm.policy != SchedPolicy::Fifo) {
        return false;
    }
    return io.prio > pcm.prio;
}

constexpr bool recorder_off_rt_cpu(const ThreadMap& m) noexcept {
    const int rt = seat_of(m, Seat::RT).cpu;
    return seat_of(m, Seat::Capture).cpu != rt && seat_of(m, Seat::Encode).cpu != rt &&
           seat_of(m, Seat::RecWrite).cpu != rt;
}

constexpr bool capture_ranks_lowest(const ThreadMap& m) noexcept {
    const ThreadRole& cap = seat_of(m, Seat::Capture);
    if (cap.policy != SchedPolicy::Fifo) return false;
    if (seat_of(m, Seat::Encode).policy != SchedPolicy::Other) return false;
    if (seat_of(m, Seat::RecWrite).policy != SchedPolicy::Other) return false;
    for (const ThreadRole& r : m) {
        if (r.seat == Seat::Capture || r.policy != SchedPolicy::Fifo) continue;
        if (r.prio <= cap.prio) return false;
    }
    return true;
}

constexpr bool policy_prio_consistent(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (r.policy == SchedPolicy::Fifo) {
            if (r.prio < 1 || r.prio > 99) return false;
        } else if (r.prio != 0) {
            return false;
        }
    }
    return true;
}

constexpr bool fifo_prios_all_distinct(const ThreadMap& m) noexcept {
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (m[i].policy != SchedPolicy::Fifo) continue;
        for (std::size_t j = i + 1; j < m.size(); ++j) {
            if (m[j].policy != SchedPolicy::Fifo) continue;
            if (m[i].prio == m[j].prio) return false;
        }
    }
    return true;
}

constexpr bool spawn_stack_consistent(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (r.spawn == SpawnKind::Create && r.stack_bytes == 0) return false;
        if (r.spawn == SpawnKind::PromoteInPlace && r.stack_bytes != 0) return false;
    }
    return true;
}

constexpr Seat highest_fifo_seat(const ThreadMap& m) noexcept {
    const ThreadRole* best = nullptr;
    for (const ThreadRole& r : m) {
        if (r.policy != SchedPolicy::Fifo) continue;
        if (best == nullptr || r.prio > best->prio) best = &r;
    }
    return best != nullptr ? best->seat : m[0].seat;
}

constexpr bool names_fit_comm(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (r.name == nullptr || name_len(r.name) == 0) return false;
        if (name_len(r.name) > kCommNameMax) return false;
    }
    return true;
}

constexpr int max_cpu(const ThreadMap& m) noexcept {
    int hi = 0;
    for (const ThreadRole& r : m) {
        if (r.cpu > hi) hi = r.cpu;
    }
    return hi;
}
constexpr bool cpus_within(const ThreadMap& m, long online_cpus) noexcept {
    if (online_cpus <= 0) return false;
    for (const ThreadRole& r : m) {
        if (r.cpu < 0 || static_cast<long>(r.cpu) >= online_cpus) return false;
    }
    return true;
}

constexpr bool main_off_rt_cpu(const ThreadMap& m, int main_cpu) noexcept {
    if (main_cpu == seat_of(m, Seat::RT).cpu) return false;
    for (const ThreadRole& r : m) {
        if (r.cpu == main_cpu) return true;
    }
    return false;
}

void adopt_placement(const ThreadMap& m, Seat seat) noexcept;

bool widen_self_affinity() noexcept;

bool restore_self_affinity() noexcept;

constexpr SeatTag tag_of(Seat s) noexcept {
    switch (s) {
        case Seat::RT:
            return SeatTag::RT;
        case Seat::Frame:
            return SeatTag::Frame;
        case Seat::Input:
            return SeatTag::Input;
        case Seat::Pcm:
            return SeatTag::Pcm;
        case Seat::Prefetch:
            return SeatTag::Prefetch;
        case Seat::Diag:
            return SeatTag::Diag;
        case Seat::Ui:
            return SeatTag::Ui;
        case Seat::Io:
            return SeatTag::Io;
        case Seat::Capture:
            return SeatTag::Capture;
        case Seat::Encode:
            return SeatTag::Encode;
        case Seat::RecWrite:
            return SeatTag::RecWrite;
    }
    return SeatTag::Unbound;
}

constexpr bool same_name(const char* a, const char* b) noexcept {
    std::size_t i = 0;
    while (a[i] != '\0' && a[i] == b[i])
        ++i;
    return a[i] == b[i];
}

constexpr bool names_match_seat_tags(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (!same_name(r.name, seat_name(tag_of(r.seat)))) return false;
    }
    return true;
}

constexpr bool every_seat_has_a_tag(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (tag_of(r.seat) == SeatTag::Unbound) return false;
    }
    return true;
}

static_assert(static_cast<std::uint8_t>(SeatTag::Unbound) == 0,
              "TRAP 1: the runtime tag's zero must NOT be a real seat. "
              "hal::Seat::RT is 0, so a tag that reused this enum's "
              "numbering would make every never-tagged thread — the process "
              "main thread and every test thread — claim T-RT, and TASTY_SEAT "
              "would be silent on exactly the crossing it exists to catch.");

static_assert(kSeatTagCount == kThreadSeats + 1,
              "one runtime tag per seat, plus Unbound. Adding a seat to this "
              "table without adding its SeatTag lands here, not at runtime.");

}  // namespace mister::hal
