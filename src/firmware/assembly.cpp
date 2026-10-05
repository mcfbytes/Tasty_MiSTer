// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembly.h"
#include "infra/posix_compat.h"

#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <ctime>
#include <span>

#include "app/input_build.h"
#include "app/input_main.h"
#include "rt_evidence.h"
#include "rt_setup.h"
#include "thread_assembly.h"
#include "os/kernel_rt_probe.h"
#include "os/small_file.h"
#include "app/capture_main.h"
#include "app/encode_main.h"
#include "app/pcm_main.h"
#include "app/rec_write_main.h"
#include "app/launcher_main.h"
#include "app/rt_main.h"
#include "svc/chd_prefetch.h"
#include "svc/prefetch_main.h"
#include "svc/io_main.h"
#include "reactor/frame_main.h"
#include "app/video_pump.h"

namespace mister::fw {

namespace {

using hal::Seat;

long sys_gettid() noexcept { return static_cast<long>(::syscall(SYS_gettid)); }

std::unexpected<Error> os_error(std::uint16_t site, int err) {
    return std::unexpected(Error{Errc::os, site, static_cast<std::uint32_t>(err)});
}

void sleep_ns(long ns) noexcept {
    timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = ns;
    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts);
    } while (rc == EINTR);
}

Ex<std::size_t> read_proc(const char* path, char* buf, std::size_t cap) {
    const auto got = os::read_small_file(path, std::span<char>(buf, cap - 1));
    if (!got) return std::unexpected(got.error());
    buf[*got] = '\0';
    return *got;
}

bool parse_u64(const char* s, std::uint64_t& out, const char** end) noexcept {
    if (s == nullptr || *s < '0' || *s > '9') return false;
    std::uint64_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + static_cast<std::uint64_t>(*s - '0');
        ++s;
    }
    out = v;
    if (end != nullptr) *end = s;
    return true;
}

void confirm_current_cpu(int cpu, RtSetup& out) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof set, &set) != 0) {
        out = RtSetup{false, errno};
        return;
    }
    const bool exact = CPU_ISSET(static_cast<std::size_t>(cpu), &set) != 0 && CPU_COUNT(&set) == 1;
    out = RtSetup{exact, exact ? 0 : ERANGE};
}

Ex<void> spawn_seat(const hal::ThreadRole& r, void* (*fn)(void*), void* arg, pthread_t& out,
                    RtMode mode, RtSetup& sched_ev) {
    pthread_attr_t attr;
    if (const int rc = ::pthread_attr_init(&attr); rc != 0) {
        return os_error(ERR_SITE(), rc);
    }
    (void)::pthread_attr_setstacksize(&attr, r.stack_bytes);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(r.cpu), &set);
    (void)::pthread_attr_setaffinity_np(&attr, sizeof set, &set);
    (void)::pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    (void)::pthread_attr_setschedpolicy(&attr, r.policy == hal::SchedPolicy::Fifo ? SCHED_FIFO
                                                                                  : SCHED_OTHER);
    sched_param sp{};
    sp.sched_priority = r.prio;
    (void)::pthread_attr_setschedparam(&attr, &sp);

    int rc = ::pthread_create(&out, &attr, fn, arg);
    if (rc == 0) {
        sched_ev = RtSetup{true, 0};
        (void)::pthread_attr_destroy(&attr);
        return {};
    }
    sched_ev = RtSetup{false, rc};
    if (mode == RtMode::Required) {
        (void)::pthread_attr_destroy(&attr);
        return os_error(ERR_SITE(), rc);
    }

    (void)::pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
    rc = ::pthread_create(&out, &attr, fn, arg);
    if (rc == 0) {
        (void)::pthread_attr_destroy(&attr);
        return {};
    }

    CPU_ZERO(&set);
    for (long c = 0; c < static_cast<long>(CPU_SETSIZE); ++c) {
        CPU_SET(static_cast<std::size_t>(c), &set);
    }
    (void)::pthread_attr_setaffinity_np(&attr, sizeof set, &set);
    rc = ::pthread_create(&out, &attr, fn, arg);
    (void)::pthread_attr_destroy(&attr);
    if (rc != 0) return os_error(ERR_SITE(), rc);
    return {};
}

template <class Ready>
[[nodiscard]] Ex<void> await_published(Ready ready) {
    for (int spins = 0; spins < 200'000; ++spins) {
        if (ready()) return {};
        (void)::sched_yield();
        if ((spins & 0x3FF) == 0x3FF) sleep_ns(1'000'000);
    }
    return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
}

}  // namespace

Ex<void> rt_memory_init(RtMode mode, RtEvidence& ev) {

    ev.kernel = os::read_kernel_rt(os::kRealtimeFlagPath);
    ev.kernel_rt = RtSetup{ev.kernel == os::KernelRt::PreemptRt, 0};
    ev.vm_compaction =
        os::read_vm_compaction(os::kCompactUnevictablePath, os::kCompactionProactivenessPath);

#if defined(__GLIBC__)
    ev.trim_pin = RtSetup{::mallopt(M_TRIM_THRESHOLD, -1) == 1, 0};
    ev.mmap_pin = RtSetup{::mallopt(M_MMAP_MAX, 0) == 1, 0};
#else
    ev.trim_pin = RtSetup{false, 0};
    ev.mmap_pin = RtSetup{false, 0};
    std::fprintf(stderr, "mallopt N/A on musl (mallocng); trim_pin/mmap_pin not attempted\n");
#endif

    if (::mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        ev.mem_lock = RtSetup{true, 0};
        return {};
    }
    ev.mem_lock = RtSetup{false, errno};
    if (mode == RtMode::Required) {

        return os_error(ERR_SITE(), ev.mem_lock.err);
    }

    return {};
}

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept {
    struct Row {
        const std::optional<long>& value;
        const char* name;
        const char* effect;
        const char* path;
    };
    const Row rows[] = {
        {vm.unevictable_allowed, "vm.compact_unevictable_allowed",
         "the kernel may move locked memory", os::kCompactUnevictablePath},
        {vm.proactiveness, "vm.compaction_proactiveness",
         "background compaction may move locked memory", os::kCompactionProactivenessPath},
    };
    for (const Row& r : rows) {
        if (!r.value || *r.value == 0) continue;
        std::fprintf(out, "%s: warning: %s=%ld: %s and stall real-time I/O; fix: echo 0 > %s\n",
                     prog, r.name, *r.value, r.effect, r.path);
    }
}

namespace {

[[gnu::noinline]] void touch_stack_pages(std::size_t span) noexcept {
    if (span == 0) return;
    volatile unsigned char* const p = static_cast<volatile unsigned char*>(__builtin_alloca(span));
    for (std::size_t off = span - 1;; off -= kStackPageBytes) {
        p[off] = 0;
        if (off < kStackPageBytes) break;
    }
    p[0] = 0;
}
}  // namespace

void prefault_current_stack(std::size_t requested, RtEvidence& ev) noexcept {
    rlimit rl{};
    if (::getrlimit(RLIMIT_STACK, &rl) != 0) {
        ev.main_stack_prefault = RtSetup{false, errno};
        return;
    }
    ev.stack_rlimit_bytes = static_cast<std::uint64_t>(rl.rlim_cur);
    pthread_attr_t attr;
    if (const int rc = ::pthread_getattr_np(::pthread_self(), &attr); rc != 0) {
        ev.main_stack_prefault = RtSetup{false, rc};
        return;
    }
    void* base = nullptr;
    std::size_t mapped = 0;
    const int grc = ::pthread_attr_getstack(&attr, &base, &mapped);
    (void)::pthread_attr_destroy(&attr);
    if (grc != 0) {
        ev.main_stack_prefault = RtSetup{false, grc};
        return;
    }
    const auto top = reinterpret_cast<std::uintptr_t>(base) + mapped;
    const auto sp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));

    const std::size_t used = (sp < top && sp >= top - mapped) ? top - sp : mapped;

    const bool growable = !kPosixGlibc && sys_gettid() == ::getpid();
    const std::size_t bound = prefault_mapped_bound(mapped, ev.stack_rlimit_bytes, growable);
    const std::size_t span = stack_prefault_span(requested, used, bound, ev.stack_rlimit_bytes);
    touch_stack_pages(span);
    ev.main_stack_prefault_bytes = span;
    ev.main_stack_prefault = RtSetup{span == requested, span == requested ? 0 : ENOMEM};
}

void pin_current_cpu(int cpu, RtSetup& out) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    if (::sched_setaffinity(0, sizeof set, &set) != 0) {
        out = RtSetup{false, errno};
        return;
    }

    confirm_current_cpu(cpu, out);
}

Ex<void> verify_cpu_topology(const hal::ThreadMap& m, long online_cpus, RtMode mode,
                             RtEvidence& ev) noexcept {
    ev.online_cpus = online_cpus;
    if (online_cpus <= 0) {

        ev.cpu_topology = RtSetup{false, errno != 0 ? errno : EINVAL};
        return {};
    }

    const long declared = static_cast<long>(hal::max_cpu(m)) + 1;
    ev.cpu_coverage = RtSetup{online_cpus == declared, online_cpus == declared ? 0 : ENOSPC};
    if (hal::cpus_within(m, online_cpus)) {
        ev.cpu_topology = RtSetup{true, 0};
        return {};
    }
    ev.cpu_topology = RtSetup{false, ERANGE};
    if (mode == RtMode::Required) {

        return os_error(ERR_SITE(), ERANGE);
    }
    return {};
}

bool cpus_in_affinity_mask(const hal::ThreadMap& m) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof set, &set) != 0) return false;
    for (const hal::ThreadRole& r : m) {
        if (r.cpu < 0 || r.cpu >= CPU_SETSIZE) return false;
        if (CPU_ISSET(static_cast<std::size_t>(r.cpu), &set) == 0) return false;
    }
    return true;
}

Ex<void> rt_topology_init(const hal::ThreadMap& m, RtMode mode, RtEvidence& ev) noexcept {
    errno = 0;
    const long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    auto r = verify_cpu_topology(m, online, mode, ev);
    if (r) return r;

    if (cpus_in_affinity_mask(m)) {

        ev.cpu_topology = RtSetup{true, EXDEV};
        return {};
    }
    return r;
}

Ex<SchedInfo> read_thread_sched(long tid) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/task/%ld/stat", tid);
    char buf[1024];
    auto n = read_proc(path, buf, sizeof buf);
    if (!n) return std::unexpected(n.error());

    const char* p = nullptr;
    for (const char* s = buf; *s != '\0'; ++s) {
        if (*s == ')') p = s;
    }
    if (p == nullptr) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    ++p;
    SchedInfo info{};
    int token = 0;
    while (*p != '\0') {
        while (*p == ' ')
            ++p;
        if (*p == '\0' || *p == '\n') break;
        const char* start = p;
        while (*p != '\0' && *p != ' ' && *p != '\n')
            ++p;
        if (token == 0) info.state = *start;
        if (token == 37 || token == 38) {
            std::uint64_t v = 0;
            if (!parse_u64(start, v, nullptr)) {
                return std::unexpected(
                    Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(token)});
            }
            if (token == 37)
                info.rt_priority = static_cast<int>(v);
            else
                info.policy = static_cast<int>(v);
        }
        ++token;
    }
    if (info.policy < 0 || info.rt_priority < 0) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(token)});
    }

    std::snprintf(path, sizeof path, "/proc/self/task/%ld/wchan", tid);
    char wbuf[64];
    if (auto w = read_proc(path, wbuf, sizeof wbuf)) {
        std::size_t len = std::strcspn(wbuf, "\n");
        if (len > 0 && !(len == 1 && wbuf[0] == '0')) {
            (void)info.wchan.assign(std::string_view(wbuf, len));
        }
    }
    return info;
}

Ex<std::uint64_t> read_vm_rss_bytes() {
    char buf[4096];
    auto n = read_proc("/proc/self/status", buf, sizeof buf);
    if (!n) return std::unexpected(n.error());
    const char* key = "VmRSS:";
    for (const char* s = buf; *s != '\0'; ++s) {
        const char* k = key;
        const char* t = s;
        while (*k != '\0' && *t == *k) {
            ++t;
            ++k;
        }
        if (*k != '\0') continue;
        while (*t == ' ' || *t == '\t')
            ++t;
        std::uint64_t kb = 0;
        if (!parse_u64(t, kb, nullptr)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        return kb * 1024u;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

ThreadAssembly::ThreadAssembly(const hal::ThreadMap& threads, xthread::RtStats& stats,
                               app::EventQueue& events, RtEvidence& boot,
                               xthread::WakeFlag& main_wake, const UiMain::Wiring& ui) noexcept
    : ev_(boot), threads_(threads), main_wake_(main_wake),
      diag_sampler_(stats, events, ev_, quiescing_, transitioning_), diag_main_(diag_sampler_),
      ui_main_(events, ui) {
    bind_ui_cells_(ui);
}

ThreadAssembly::ThreadAssembly(xthread::RtStats& stats, app::EventQueue& events,
                               const UiMain::Wiring& ui) noexcept
    : ev_(own_ev_), threads_(hal::kDe10ThreadMap), main_wake_(own_main_wake_),
      diag_sampler_(stats, events, ev_, quiescing_, transitioning_), diag_main_(diag_sampler_),
      ui_main_(events, ui) {
    bind_ui_cells_(ui);
}

bool ThreadAssembly::any_live() const noexcept {
    return diag_live_ || ui_live_ || frame_live_ || input_live_ || prefetch_live_ || pcm_live_ ||
           io_live_ || rt_live_ || capture_live_ || encode_live_ || rec_write_live_ ||
           launcher_live_;
}

ThreadAssembly::~ThreadAssembly() {
    if (any_live()) {
        stop();
        (void)join();
    }
}

void ThreadAssembly::bind_ui_cells_(const UiMain::Wiring& ui) noexcept {
    diag_sampler_.set_cmd_fifo_cell(ui.fifo != nullptr ? &ui.fifo->stats_cell() : nullptr);
    diag_sampler_.set_mgl_cell(ui.mgl != nullptr ? &ui.mgl->stats_cell() : nullptr);
    diag_sampler_.set_video_stats_cell(ui.video != nullptr ? &ui.video->stats_cell() : nullptr);
    diag_sampler_.set_video_geometry_cell(ui.video != nullptr ? &ui.video->geometry_cell()
                                                              : nullptr);

    diag_sampler_.set_video_wire(ui.video != nullptr ? &ui.video->wire() : nullptr);
}

void ThreadAssembly::adopt_seat(hal::Seat s) noexcept {
    const hal::ThreadRole& r = hal::seat_of(threads_, s);

    const int nrc = ::pthread_setname_np(::pthread_self(), r.name);
    ev_.seat_name[static_cast<std::size_t>(s)] = RtSetup{nrc == 0, nrc};
    if (RtSetup* aff = affinity_row(s); aff != nullptr) {

        if (r.spawn == hal::SpawnKind::PromoteInPlace || !kPosixGlibc) {
            pin_current_cpu(r.cpu, *aff);
        } else {
            confirm_current_cpu(r.cpu, *aff);
        }
    }

    hal::adopt_placement(threads_, s);
    (void)adopt_seat_tag(hal::tag_of(s));
}

RtSetup* ThreadAssembly::affinity_row(hal::Seat s) noexcept {
    switch (s) {
        case Seat::RT:
            return &ev_.rt_affinity;
        case Seat::Frame:
            return &ev_.frame_affinity;
        case Seat::Input:
            return &ev_.input_affinity;
        case Seat::Diag:
            return &ev_.diag_affinity;
        case Seat::Ui:
            return &ev_.ui_affinity;
        case Seat::Prefetch:
            return &ev_.prefetch_affinity;
        case Seat::Pcm:
            return &ev_.pcm_affinity;
        case Seat::Io:
            return &ev_.io_affinity;
        case Seat::Capture:
            return &ev_.capture_affinity;
        case Seat::Encode:
            return &ev_.encode_affinity;
        case Seat::RecWrite:
            return &ev_.recwrite_affinity;
        case Seat::Launcher:
            return &ev_.launcher_affinity;
    }
    return nullptr;
}

DiagMain& ThreadAssembly::main_(std::type_identity<DiagMain>) noexcept { return diag_main_; }
UiMain& ThreadAssembly::main_(std::type_identity<UiMain>) noexcept { return ui_main_; }
svc::PrefetchMain& ThreadAssembly::main_(std::type_identity<svc::PrefetchMain>) noexcept {
    return *prefetch_;
}
app::PcmMain& ThreadAssembly::main_(std::type_identity<app::PcmMain>) noexcept { return *pcm_; }
reactor::FrameMain& ThreadAssembly::main_(std::type_identity<reactor::FrameMain>) noexcept {
    return *frame_;
}
app::InputMain& ThreadAssembly::main_(std::type_identity<app::InputMain>) noexcept {
    return *input_;
}
svc::IoMain& ThreadAssembly::main_(std::type_identity<svc::IoMain>) noexcept { return *io_; }
app::RtMain& ThreadAssembly::main_(std::type_identity<app::RtMain>) noexcept { return *rt_; }
app::CaptureMain& ThreadAssembly::main_(std::type_identity<app::CaptureMain>) noexcept {
    return *capture_;
}
app::EncodeMain& ThreadAssembly::main_(std::type_identity<app::EncodeMain>) noexcept {
    return *encode_;
}
app::RecWriteMain& ThreadAssembly::main_(std::type_identity<app::RecWriteMain>) noexcept {
    return *rec_write_;
}
app::LauncherMain& ThreadAssembly::main_(std::type_identity<app::LauncherMain>) noexcept {
    return *launcher_;
}

constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<DiagMain>) noexcept {
    return DiagMain::kSeat;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<UiMain>) noexcept {
    return UiMain::kSeat;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<svc::PrefetchMain>) noexcept {
    return Seat::Prefetch;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::PcmMain>) noexcept {
    return Seat::Pcm;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<reactor::FrameMain>) noexcept {
    return Seat::Frame;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::InputMain>) noexcept {
    return Seat::Input;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<svc::IoMain>) noexcept {
    return Seat::Io;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::RtMain>) noexcept {
    return Seat::RT;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::CaptureMain>) noexcept {
    return Seat::Capture;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::EncodeMain>) noexcept {
    return Seat::Encode;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::RecWriteMain>) noexcept {
    return Seat::RecWrite;
}
constexpr hal::Seat ThreadAssembly::seat_(std::type_identity<app::LauncherMain>) noexcept {
    return Seat::Launcher;
}

std::atomic<long>* ThreadAssembly::tid_cell_(hal::Seat s) noexcept {
    switch (s) {
        case Seat::RT:
            return &rt_tid_;
        case Seat::Frame:
            return &frame_tid_;
        case Seat::Input:
            return &input_tid_;
        case Seat::Diag:
            return &diag_tid_;
        case Seat::Ui:
            return &ui_tid_;
        case Seat::Prefetch:
            return &prefetch_tid_;
        case Seat::Pcm:
            return &pcm_tid_;
        case Seat::Io:
            return &io_tid_;
        case Seat::Capture:
            return &capture_tid_;
        case Seat::Encode:
            return &encode_tid_;
        case Seat::RecWrite:
            return &rec_write_tid_;
        case Seat::Launcher:
            return &launcher_tid_;
    }
    return nullptr;
}

template <class M>
void* ThreadAssembly::trampoline(void* self) {
    static_assert(xthread::SeatBody<M>);
    constexpr hal::Seat s = seat_(std::type_identity<M>{});
    auto* a = static_cast<ThreadAssembly*>(self);
    a->adopt_seat(s);
    a->tid_cell_(s)->store(sys_gettid(), std::memory_order_release);
    a->main_(std::type_identity<M>{}).start();
    if constexpr (s == Seat::RT) {

        a->rt_result_ = a->rt_->result();
        a->rt_exited_.store(true, std::memory_order_release);
        a->main_wake_.request();
    }
    if constexpr (s == Seat::Io) {
        a->io_exited_.store(true, std::memory_order_release);
    }
    return nullptr;
}

Ex<void> ThreadAssembly::spawn(RtMode mode, const SeatMains& mains) {
    frame_ = mains.frame;
    input_ = mains.input;
    prefetch_ = mains.prefetch;
    pcm_ = mains.pcm;
    io_ = mains.io;
    const bool recorder =
        mains.capture != nullptr && mains.encode != nullptr && mains.rec_write != nullptr;
    capture_ = recorder ? mains.capture : nullptr;
    encode_ = recorder ? mains.encode : nullptr;
    rec_write_ = recorder ? mains.rec_write : nullptr;
    launcher_ = mains.launcher;
    diag_sampler_.set_input_build(mains.input_build);
    diag_sampler_.set_prefetch(prefetch_ != nullptr ? &prefetch_->prefetch() : nullptr);

    if (auto o = diag_main_.open(); !o) return o;
    if (auto o = ui_main_.open(); !o) return o;

    if (auto r =
            spawn_seat(hal::seat_of(threads_, Seat::Diag), &ThreadAssembly::trampoline<DiagMain>,
                       this, diag_thread_, mode, ev_.diag_sched);
        !r) {
        return r;
    }
    diag_live_ = true;

    if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Ui), &ThreadAssembly::trampoline<UiMain>,
                            this, ui_thread_, mode, ev_.ui_sched);
        !r) {
        return r;
    }
    ui_live_ = true;

    if (prefetch_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Prefetch),
                                &ThreadAssembly::trampoline<svc::PrefetchMain>, this,
                                prefetch_thread_, mode, ev_.prefetch_fifo);
            !r) {
            return r;
        }
        prefetch_live_ = true;
    }

    if (pcm_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Pcm),
                                &ThreadAssembly::trampoline<app::PcmMain>, this, pcm_thread_, mode,
                                ev_.pcm_fifo);
            !r) {
            return r;
        }
        pcm_live_ = true;
    }

    if (frame_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Frame),
                                &ThreadAssembly::trampoline<reactor::FrameMain>, this,
                                frame_thread_, mode, ev_.frame_fifo);
            !r) {
            return r;
        }
        frame_live_ = true;
    }

    if (input_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Input),
                                &ThreadAssembly::trampoline<app::InputMain>, this, input_thread_,
                                mode, ev_.input_fifo);
            !r) {
            return r;
        }
        input_live_ = true;
    }

    if (io_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Io),
                                &ThreadAssembly::trampoline<svc::IoMain>, this, io_thread_, mode,
                                ev_.io_fifo);
            !r) {
            return r;
        }
        io_live_ = true;
    }

    if (rec_write_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::RecWrite),
                                &ThreadAssembly::trampoline<app::RecWriteMain>, this,
                                rec_write_thread_, mode, ev_.recwrite_sched);
            !r) {
            return r;
        }
        rec_write_live_ = true;
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Encode),
                                &ThreadAssembly::trampoline<app::EncodeMain>, this, encode_thread_,
                                mode, ev_.encode_sched);
            !r) {
            return r;
        }
        encode_live_ = true;
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Capture),
                                &ThreadAssembly::trampoline<app::CaptureMain>, this,
                                capture_thread_, mode, ev_.capture_fifo);
            !r) {
            return r;
        }
        capture_live_ = true;
    }

    if (launcher_ != nullptr) {
        if (auto r = spawn_seat(hal::seat_of(threads_, Seat::Launcher),
                                &ThreadAssembly::trampoline<app::LauncherMain>, this,
                                launcher_thread_, mode, ev_.launcher_sched);
            !r) {
            return r;
        }
        launcher_live_ = true;
    }

    return await_published([this] {
        return diag_tid() != 0 && ui_tid() != 0 && (frame_ == nullptr || frame_tid() != 0) &&
               (prefetch_ == nullptr || prefetch_tid() != 0) &&
               (pcm_ == nullptr || pcm_tid() != 0) && (input_ == nullptr || input_tid() != 0) &&
               (io_ == nullptr || io_tid() != 0) &&
               (capture_ == nullptr ||
                (capture_tid() != 0 && encode_tid() != 0 && rec_write_tid() != 0)) &&
               (launcher_ == nullptr || launcher_tid() != 0);
    });
}

Ex<void> ThreadAssembly::spawn_rt(RtMode mode, app::RtMain& rt) {

    rt_ = &rt;
    if (auto r =
            spawn_seat(hal::seat_of(threads_, Seat::RT), &ThreadAssembly::trampoline<app::RtMain>,
                       this, rt_thread_, mode, ev_.rt_fifo);
        !r) {
        return r;
    }
    rt_live_ = true;
    return await_published([this] { return rt_tid() != 0; });
}

Ex<void> ThreadAssembly::join_rt_within(std::int64_t deadline_ns) {
    if (!rt_live_) return {};
    timespec abs{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &abs);
    const std::int64_t total = static_cast<std::int64_t>(abs.tv_nsec) + deadline_ns;
    abs.tv_sec += static_cast<time_t>(total / 1'000'000'000);
    abs.tv_nsec = static_cast<long>(total % 1'000'000'000);
#if defined(__GLIBC__)
    const int rc = ::pthread_clockjoin_np(rt_thread_, nullptr, CLOCK_MONOTONIC, &abs);
#else
    int rc = 0;
    for (;;) {
        if (rt_exited_.load(std::memory_order_acquire)) {
            rc = ::pthread_join(rt_thread_, nullptr);
            break;
        }
        timespec now{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > abs.tv_sec || (now.tv_sec == abs.tv_sec && now.tv_nsec >= abs.tv_nsec)) {
            rc = ETIMEDOUT;
            break;
        }
        timespec slp{};
        slp.tv_nsec = 1'000'000;
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, nullptr);
    }
#endif
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{Errc::timeout, ERR_SITE(), static_cast<std::uint32_t>(deadline_ns / 1'000'000)});
    }
    if (rc != 0) return os_error(ERR_SITE(), rc);
    rt_live_ = false;
    return {};
}

Ex<void> ThreadAssembly::join_io_within(std::int64_t deadline_ns) {
    if (!io_live_) return {};
    timespec abs{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &abs);
    const std::int64_t total = static_cast<std::int64_t>(abs.tv_nsec) + deadline_ns;
    abs.tv_sec += static_cast<time_t>(total / 1'000'000'000);
    abs.tv_nsec = static_cast<long>(total % 1'000'000'000);
#if defined(__GLIBC__)
    const int rc = ::pthread_clockjoin_np(io_thread_, nullptr, CLOCK_MONOTONIC, &abs);
#else
    int rc = 0;
    for (;;) {
        if (io_exited_.load(std::memory_order_acquire)) {
            rc = ::pthread_join(io_thread_, nullptr);
            break;
        }
        timespec now{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > abs.tv_sec || (now.tv_sec == abs.tv_sec && now.tv_nsec >= abs.tv_nsec)) {
            rc = ETIMEDOUT;
            break;
        }
        timespec slp{};
        slp.tv_nsec = 1'000'000;
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, nullptr);
    }
#endif
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{Errc::timeout, ERR_SITE(), static_cast<std::uint32_t>(deadline_ns / 1'000'000)});
    }
    if (rc != 0) return os_error(ERR_SITE(), rc);
    io_live_ = false;
    return {};
}

Ex<void> ThreadAssembly::stop_and_join_rt(std::int64_t deadline_ns) {
    if (!rt_live_) return {};
    timespec now{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &now);
    const std::int64_t hard =
        static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec + deadline_ns;
    for (;;) {

        rt_->stop();
        (void)::clock_gettime(CLOCK_MONOTONIC, &now);
        const std::int64_t now_ns =
            static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
        std::int64_t rem = hard - now_ns;
        if (rem <= 0) return join_rt_within(0);
        if (rem > kRtJoinSliceNs) rem = kRtJoinSliceNs;
        if (auto r = join_rt_within(rem); r)
            return r;
        else if (r.error().code != Errc::timeout)
            return r;
    }
}

void ThreadAssembly::stop() noexcept {

    if (rt_live_) rt_->stop();
    diag_main_.stop();
    ui_main_.stop();
    if (frame_ != nullptr) frame_->stop();

    if (input_ != nullptr) input_->stop();

    if (prefetch_ != nullptr) prefetch_->stop();

    if (pcm_ != nullptr) pcm_->stop();

    if (io_ != nullptr) io_->stop();

    if (capture_ != nullptr) capture_->stop();
    if (launcher_ != nullptr) launcher_->stop();
}

void ThreadAssembly::mark_quiescing() noexcept { quiescing_.request(); }

void ThreadAssembly::mark_transitioning(bool on) noexcept {

    transitioning_.store(on, std::memory_order_release);
}

Ex<void> ThreadAssembly::join() {

    if (rt_live_) {

        if (auto r = stop_and_join_rt(kRtStopDeadlineNs); !r) return r;
    }

    Ex<void> io_err{};
    if (io_live_ && !io_poisoned_) {
        if (auto r = join_io_within(kIoStopDeadlineNs); !r) {
            if (r.error().code == Errc::timeout) {

                io_poisoned_ = true;
            }
            io_err = std::unexpected(r.error());
        }
    }

    if (capture_live_) {
        if (const int rc = ::pthread_join(capture_thread_, nullptr); rc != 0)
            return os_error(ERR_SITE(), rc);
        capture_live_ = false;
    }
    if (encode_ != nullptr) encode_->stop();
    if (encode_live_) {
        if (const int rc = ::pthread_join(encode_thread_, nullptr); rc != 0)
            return os_error(ERR_SITE(), rc);
        encode_live_ = false;
    }
    if (rec_write_ != nullptr) rec_write_->stop();
    if (rec_write_live_) {
        if (const int rc = ::pthread_join(rec_write_thread_, nullptr); rc != 0)
            return os_error(ERR_SITE(), rc);
        rec_write_live_ = false;
    }
    if (launcher_live_) {
        if (const int rc = ::pthread_join(launcher_thread_, nullptr); rc != 0)
            return os_error(ERR_SITE(), rc);
        launcher_live_ = false;
    }
    if (pcm_live_) {
        if (const int rc = ::pthread_join(pcm_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        pcm_live_ = false;
    }
    if (prefetch_live_) {
        if (const int rc = ::pthread_join(prefetch_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        prefetch_live_ = false;
    }
    if (frame_live_) {
        if (const int rc = ::pthread_join(frame_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        frame_live_ = false;
    }

    if (input_live_) {
        if (const int rc = ::pthread_join(input_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        input_live_ = false;
    }
    if (ui_live_) {
        if (const int rc = ::pthread_join(ui_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        ui_live_ = false;
    }
    if (diag_live_) {
        if (const int rc = ::pthread_join(diag_thread_, nullptr); rc != 0) {
            return os_error(ERR_SITE(), rc);
        }
        diag_live_ = false;
    }

    return io_err;
}

}  // namespace mister::fw
