// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembly.h"
#include "infra/posix_compat.h"

#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
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
#include "app/hd_osd_main.h"
#include "app/rt_main.h"
#include "svc/chd_prefetch.h"
#include "svc/prefetch_main.h"
#include "svc/io_main.h"
#include "reactor/frame_main.h"
#include "app/video_pump.h"

namespace mister::fw {

namespace {

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

struct ThreadAssembly::SeatTable {
    struct Entry {
        void* (*body)(void*) = nullptr;
    };
    using Entries = std::array<Entry, hal::kThreadSeats>;

    template <class... Rows>
    static consteval Entries make(SeatList<Rows...>) {
        Entries t{};
        ((t[seat_index(Rows::seat)] = Entry{&ThreadAssembly::trampoline<Rows>}), ...);
        return t;
    }

    static const Entries kSeatEntries;

    static const Entry& of(SeatTag s) noexcept { return kSeatEntries[seat_index(s)]; }
};

constexpr ThreadAssembly::SeatTable::Entries ThreadAssembly::SeatTable::kSeatEntries =
    ThreadAssembly::SeatTable::make(SeatMainList{});

ThreadAssembly::ThreadAssembly(const hal::ThreadMap& threads, xthread::RtStats& stats,
                               app::EventQueue& events, RtEvidence& boot,
                               xthread::WakeFlag& main_wake, const DiagWires& diag,
                               const UiMain::Wires& ui_wires, const UiMain::Wiring& ui,
                               const DiagSampler::Sources& diag_sources) noexcept
    : ev_(boot), threads_(threads), main_wake_(main_wake),
      diag_sampler_(stats, events, ev_, quiescing_, diag.transitioning, diag.log, diag.rt_lane,
                    with_ui_cells_(diag_sources, ui)) {
    create_mains_(events, ui_wires, ui);
}

ThreadAssembly::ThreadAssembly(xthread::RtStats& stats, app::EventQueue& events,
                               const UiMain::Wiring& ui, const DiagSampler::Sources& diag) noexcept
    : ev_(own_ev_), threads_(hal::kDe10ThreadMap), main_wake_(own_main_wake_),
      own_wires_(std::make_unique<OwnWires>()),
      diag_sampler_(stats, events, ev_, quiescing_, own_wires_->transitioning, own_wires_->log,
                    own_wires_->rt_lane, with_ui_cells_(diag, ui)) {
    create_mains_(events, UiMain::Wires{own_wires_->ui_wake, own_wires_->uart_handoffs}, ui);
}

void ThreadAssembly::create_mains_(app::EventQueue& events, const UiMain::Wires& ui_wires,
                                   const UiMain::Wiring& ui) noexcept {
    auto diag_fds = xthread::ParkFds::create(diag_wake_);
    if (!diag_fds) {
        born_ = std::unexpected(diag_fds.error());
        return;
    }
    auto ui_fds = xthread::ParkFds::create(ui_wires.wake);
    if (!ui_fds) {
        born_ = std::unexpected(ui_fds.error());
        return;
    }
    diag_main_.emplace(diag_sampler_, std::move(*diag_fds));
    ui_main_.emplace(events, std::move(*ui_fds), ui_wires.uart_handoffs, ui, main_wake_);
    mains_.bind(&*diag_main_);
    mains_.bind(&*ui_main_);
}

bool ThreadAssembly::any_live() const noexcept {
    for (const bool live : live_) {
        if (live) return true;
    }
    return false;
}

ThreadAssembly::~ThreadAssembly() { stop_join_or_exit(); }

namespace {
constexpr ThreadAssembly::StopBudget kShippingBudget{.rt_ns = kRtStopDeadlineNs,
                                                     .io_ns = kIoStopDeadlineNs};
}

void ThreadAssembly::stop_join_or_exit() noexcept { stop_join_or_exit(kShippingBudget); }

void ThreadAssembly::stop_join_or_exit(StopBudget budget) noexcept {
    if (!any_live()) return;
    stop();
    (void)join(budget);
    if (any_live()) exit_seats_live_();
}

void ThreadAssembly::exit_seats_live_() const noexcept {
    for (const SeatTag s : kJoinOrder) {
        if (!live_[seat_index(s)]) continue;
        char line[128];
        const int n = std::snprintf(line, sizeof line,
                                    "{\"t\":\"wedge\",\"seat\":\"%s\",\"tid\":%ld,"
                                    "\"at\":\"teardown\",\"exit\":%d}\n",
                                    seat_name(s), tid(s), kRtWedgeExitStatus);
        if (n > 0) {
            const auto len = std::min(static_cast<std::size_t>(n), sizeof line - 1);
            const auto w = ::write(STDERR_FILENO, line, len);
            (void)w;
        }
    }
    ::_exit(kRtWedgeExitStatus);
}

DiagSampler::Sources ThreadAssembly::with_ui_cells_(DiagSampler::Sources s,
                                                    const UiMain::Wiring& ui) noexcept {
    return DiagSampler::Sources{
        .fifo = ui.fifo != nullptr ? &ui.fifo->stats_cell() : nullptr,
        .mgl = ui.mgl != nullptr ? &ui.mgl->stats_cell() : nullptr,
        .mgl_row0 = s.mgl_row0,
        .window_counts = s.window_counts,
        .video_stats = ui.video.stats_cell(),
        .video_geometry = ui.video.geometry_cell(),
        .video_wire = ui.video.wire(),
        .diag = s.diag,
        .pause_expiries = s.pause_expiries,
        .recover_polls = s.recover_polls,
        .save_write_failures = s.save_write_failures,
        .fallbacks = s.fallbacks,
        .doorbell = s.doorbell,
        .round_timing = s.round_timing,
        .frames = s.frames,
        .ui_pages = s.ui_pages,
        .hd = s.hd,
        .replay = s.replay,
        .rec_capture = s.rec_capture,
        .rec_encode = s.rec_encode,
        .rec_write = s.rec_write,
        .rec_avi = s.rec_avi,
        .screenshots = s.screenshots,
    };
}

std::array<char, hal::kCommNameMax + 1> ThreadAssembly::process_comm_() noexcept {
    std::array<char, hal::kCommNameMax + 1> out{};
    if (::prctl(PR_GET_NAME, out.data(), 0, 0, 0) != 0) out.fill('\0');
    return out;
}

void ThreadAssembly::adopt_seat(SeatTag s) noexcept {
    const hal::ThreadRole& r = hal::seat_of(threads_, s);

    const hal::CommName comm = hal::comm_name(comm_prefix_.data(), seat_role(s));
    const int nrc = ::pthread_setname_np(::pthread_self(), comm.text.data());
    ev_.seat_name[seat_index(s)] = RtSetup{nrc == 0, nrc};

    RtSetup& aff = ev_.seat_affinity[seat_index(s)];
    if (r.spawn == hal::SpawnKind::PromoteInPlace || !kPosixGlibc) {
        pin_current_cpu(r.cpu, aff);
    } else {
        confirm_current_cpu(r.cpu, aff);
    }

    hal::adopt_placement(threads_, s);
    (void)adopt_seat_tag(s);
}

template <class Row>
void ThreadAssembly::stop_one_() noexcept {
    if (auto* m = mains_.get<typename Row::Main>(); m != nullptr) m->stop();
}

template <class... Rows>
void ThreadAssembly::stop_seat_(SeatTag s, SeatList<Rows...>) noexcept {
    ((Rows::seat == s ? stop_one_<Rows>() : void()), ...);
}

void ThreadAssembly::stop_bound_(SeatTag s) noexcept { stop_seat_(s, SeatMainList{}); }

template <class Row>
void* ThreadAssembly::trampoline(void* self) {
    using M = typename Row::Main;
    static_assert(xthread::SeatBody<M>);
    constexpr SeatTag s = Row::seat;
    auto* a = static_cast<ThreadAssembly*>(self);
    a->adopt_seat(s);
    a->tid_[seat_index(s)].store(sys_gettid(), std::memory_order_release);
    a->mains_.get<M>()->start();
    if constexpr (s == SeatTag::RT) {

        a->rt_result_ = a->mains_.get<M>()->result();
        a->rt_exited_.store(true, std::memory_order_release);
        a->main_wake_.request();
    }
    if constexpr (s == SeatTag::Io) {
        a->io_exited_.store(true, std::memory_order_release);
    }
    return nullptr;
}

Ex<void> ThreadAssembly::spawn(RtMode mode, const SeatMains& mains) {

    app::RtMain* const rt = mains_.get<app::RtMain>();
    mains_ = mains.mains;
    if (!born_) return born_;
    mains_.bind(&*diag_main_);
    mains_.bind(&*ui_main_);
    mains_.bind(rt);

    if (!(mains_.bound(SeatTag::Capture) && mains_.bound(SeatTag::Encode) &&
          mains_.bound(SeatTag::RecWrite))) {
        mains_.bind(static_cast<app::CaptureMain*>(nullptr));
        mains_.bind(static_cast<app::EncodeMain*>(nullptr));
        mains_.bind(static_cast<app::RecWriteMain*>(nullptr));
    }
    svc::PrefetchMain* const prefetch = mains_.get<svc::PrefetchMain>();
    diag_sampler_.set_input_build(mains.input_build);
    diag_sampler_.set_prefetch(prefetch != nullptr ? &prefetch->prefetch() : nullptr);

    for (const SeatTag s : std::span(kSpawnOrder).first(kSpawnOrder.size() - 1)) {
        if (!mains_.bound(s)) continue;
        const std::size_t i = seat_index(s);
        if (auto r = spawn_seat(hal::seat_of(threads_, s), SeatTable::of(s).body, this, thread_[i],
                                mode, ev_.seat_sched[i]);
            !r) {
            return r;
        }
        live_[i] = true;
    }

    return await_published([this] {
        for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
            if (live_[i] && tid_[i].load(std::memory_order_acquire) == 0) return false;
        }
        return true;
    });
}

Ex<void> ThreadAssembly::spawn_rt(RtMode mode, app::RtMain& rt) {

    mains_.bind(&rt);
    const std::size_t i = seat_index(SeatTag::RT);
    if (auto r = spawn_seat(hal::seat_of(threads_, SeatTag::RT), SeatTable::of(SeatTag::RT).body,
                            this, thread_[i], mode, ev_.seat_sched[i]);
        !r) {
        return r;
    }
    live_[i] = true;
    return await_published([this] { return tid(SeatTag::RT) != 0; });
}

namespace {

int join_within(pthread_t thread, [[maybe_unused]] const std::atomic<bool>& exited,
                const timespec& abs) noexcept {
#if defined(__GLIBC__)
    return ::pthread_clockjoin_np(thread, nullptr, CLOCK_MONOTONIC, &abs);
#else
    for (;;) {
        if (exited.load(std::memory_order_acquire)) return ::pthread_join(thread, nullptr);
        timespec now{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > abs.tv_sec || (now.tv_sec == abs.tv_sec && now.tv_nsec >= abs.tv_nsec)) {
            return ETIMEDOUT;
        }
        timespec slp{};
        slp.tv_nsec = 1'000'000;
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, nullptr);
    }
#endif
}

timespec deadline_from_now(std::int64_t deadline_ns) noexcept {
    timespec abs{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &abs);
    const std::int64_t total = static_cast<std::int64_t>(abs.tv_nsec) + deadline_ns;
    abs.tv_sec += static_cast<time_t>(total / 1'000'000'000);
    abs.tv_nsec = static_cast<long>(total % 1'000'000'000);
    return abs;
}

}  // namespace

Ex<void> ThreadAssembly::join_rt_within(std::int64_t deadline_ns) {
    const std::size_t i = seat_index(SeatTag::RT);
    if (!live_[i]) return {};
    const int rc = join_within(thread_[i], rt_exited_, deadline_from_now(deadline_ns));
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{Errc::timeout, ERR_SITE(), static_cast<std::uint32_t>(deadline_ns / 1'000'000)});
    }
    if (rc != 0) return os_error(ERR_SITE(), rc);
    live_[i] = false;
    return {};
}

Ex<void> ThreadAssembly::join_io_within(std::int64_t deadline_ns) {
    const std::size_t i = seat_index(SeatTag::Io);
    if (!live_[i]) return {};
    const int rc = join_within(thread_[i], io_exited_, deadline_from_now(deadline_ns));
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{Errc::timeout, ERR_SITE(), static_cast<std::uint32_t>(deadline_ns / 1'000'000)});
    }
    if (rc != 0) return os_error(ERR_SITE(), rc);
    live_[i] = false;
    return {};
}

Ex<void> ThreadAssembly::stop_and_join_rt(std::int64_t deadline_ns) {
    if (!live_[seat_index(SeatTag::RT)]) return {};
    timespec now{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &now);
    const std::int64_t hard =
        static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec + deadline_ns;
    for (;;) {

        mains_.get<app::RtMain>()->stop();
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

    for (const SeatTag s : kStopOrder) {
        if (s == SeatTag::RT && !live_[seat_index(s)]) continue;
        stop_bound_(s);
    }
}

void ThreadAssembly::mark_quiescing() noexcept { quiescing_.request(); }

Ex<void> ThreadAssembly::join() { return join(kShippingBudget); }

Ex<void> ThreadAssembly::join(StopBudget budget) {

    static_assert(kJoinOrder[0] == SeatTag::RT && kJoinOrder[1] == SeatTag::Io);
    if (live_[seat_index(SeatTag::RT)]) {

        if (auto r = stop_and_join_rt(budget.rt_ns); !r) return r;
    }

    Ex<void> err{};
    if (live_[seat_index(SeatTag::Io)] && !io_poisoned_) {
        if (auto r = join_io_within(budget.io_ns); !r) {
            if (r.error().code == Errc::timeout) {

                io_poisoned_ = true;
            }
            err = std::unexpected(r.error());
        }
    }

    for (const SeatTag s : std::span(kJoinOrder).subspan(2)) {
        if (drains_after_join(kStopAfterJoinOf, s)) stop_bound_(s);
        const std::size_t i = seat_index(s);
        if (!live_[i]) continue;
        if (const int rc = ::pthread_join(thread_[i], nullptr); rc != 0) {
            if (err) err = os_error(ERR_SITE(), rc);
            continue;
        }
        live_[i] = false;
    }

    return err;
}

}  // namespace mister::fw
