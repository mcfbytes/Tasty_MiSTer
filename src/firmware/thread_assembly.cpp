// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_assembly.h"

#include "assembly.h"
#include "wedge_record.h"
#include "infra/posix_compat.h"

#include "infra/seat_role.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string_view>

#include "app/capture_main.h"
#include "app/connect_main.h"
#include "app/encode_main.h"
#include "app/hd_osd_main.h"
#include "app/input_build.h"
#include "app/input_main.h"
#include "app/launcher_main.h"
#include "app/pcm_main.h"
#include "app/rec_write_main.h"
#include "app/rt_main.h"
#include "app/video_pump.h"
#include "os/kernel_tid.h"
#include "os/timer_slack.h"
#include "os/types.h"
#include "reactor/frame_main.h"
#include "rt_evidence.h"
#include "rt_setup.h"
#include "svc/chd_prefetch.h"
#include "svc/io_main.h"
#include "svc/prefetch_main.h"

namespace mister::fw {

namespace {

constexpr std::chrono::nanoseconds kOneSecond{std::chrono::seconds{1}};
constexpr std::chrono::nanoseconds kOneMilli{std::chrono::milliseconds{1}};

void sleep_one_milli() noexcept {
    timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = kOneMilli.count();
    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts);
    } while (rc == EINTR);
}

void block_process_stop_set() noexcept {
    sigset_t set;
    ::sigemptyset(&set);
    for (const int sig : {SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE})
        (void)::sigaddset(&set, sig);
    (void)::pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

struct SpawnOutcome {
    Ex<pthread_t> handle{};
    RtSetup sched{};
};

[[nodiscard]] SpawnOutcome spawn_seat(
    const hal::ThreadRole& r,
    void* (*fn)(void*),
    void* arg,
    RtMode mode
) {
    pthread_attr_t attr;
    if (const int rc = ::pthread_attr_init(&attr); rc != 0) {
        return {std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc)), RtSetup{}};
    }
    pthread_t handle{};
    (void)::pthread_attr_setstacksize(&attr, r.stack_bytes.bytes());
    cpu_set_t set = os::cpu_set_of(r.cpu);
    (void)infra::pthread_attr_setaffinity_np(&attr, sizeof set, &set);
    (void)::pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    (void)::pthread_attr_setschedpolicy(
        &attr,
        r.policy == hal::SchedPolicy::Fifo ? SCHED_FIFO : SCHED_OTHER
    );
    sched_param sp{};
    sp.sched_priority = r.prio;
    (void)::pthread_attr_setschedparam(&attr, &sp);

    int rc = ::pthread_create(&handle, &attr, fn, arg);
    if (rc == 0) {
        (void)::pthread_attr_destroy(&attr);
        return {handle, RtSetup{true, 0}};
    }
    const RtSetup sched{false, rc};
    if (mode == RtMode::Required) {
        (void)::pthread_attr_destroy(&attr);
        return {std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc)), sched};
    }

    (void)::pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
    rc = ::pthread_create(&handle, &attr, fn, arg);
    if (rc == 0) {
        (void)::pthread_attr_destroy(&attr);
        return {handle, sched};
    }

    CPU_ZERO(&set);
    for (long c = 0; c < static_cast<long>(CPU_SETSIZE); ++c) {
        CPU_SET(static_cast<std::size_t>(c), &set);
    }
    (void)infra::pthread_attr_setaffinity_np(&attr, sizeof set, &set);
    rc = ::pthread_create(&handle, &attr, fn, arg);
    (void)::pthread_attr_destroy(&attr);
    if (rc != 0)
        return {std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc)), sched};
    return {handle, sched};
}

template <class Ready>
[[nodiscard]] Ex<void> await_published(Ready ready) {
    constexpr int kMaxSpins = 200'000;
    constexpr int kSleepEveryMask = 0x3FF;
    for (int spins = 0; spins < kMaxSpins; ++spins) {
        if (ready())
            return {};
        (void)::sched_yield();
        if ((spins & kSleepEveryMask) == kSleepEveryMask)
            sleep_one_milli();
    }
    return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
}

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

constexpr ThreadAssembly::SeatTable::Entries
    ThreadAssembly::SeatTable::kSeatEntries = ThreadAssembly::SeatTable::make(SeatMainList{});

ThreadAssembly::ThreadAssembly(
    const hal::ThreadMap& threads,
    xthread::RtStats& stats,
    app::EventQueue<SeatTag::RT>& events,
    RtEvidence& boot,
    xthread::WakeFlag& main_wake,
    const DiagWires& diag,
    const UiMain::Wires& ui_wires,
    const UiMain::Wiring& ui,
    const DiagSampler::Sources& diag_sources,
    const DiagSampler::Services& diag_services,
    SeatContract contract
) noexcept
    : contract_(contract),
      ev_(boot),
      threads_(threads),
      main_wake_(main_wake),
      diag_log_(diag.log),
      diag_sampler_(
          DiagSampler::Wiring{
              .stats = stats,
              .events = events,
              .ev = ev_cell_,
              .quiescing = quiescing_,
              .transitioning = diag.transitioning,
              .diag = diag.log,
              .rt_lane = diag.rt_lane
          },
          with_ui_cells_(diag_sources, ui),
          diag_services
      ) {
    create_mains_(events, ui_wires, ui);
}

void ThreadAssembly::publish_evidence_() noexcept {
    ev_cell_.publish(ev_);
}

void ThreadAssembly::create_mains_(
    app::EventQueue<SeatTag::RT>& events,
    const UiMain::Wires& ui_wires,
    const UiMain::Wiring& ui
) noexcept {
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
    for (const LiveSeat& seat : seats_) {
        if (seat.live)
            return true;
    }
    return false;
}

ThreadAssembly::~ThreadAssembly() {
    stop_join_or_exit();
}

namespace {
constexpr ThreadAssembly::StopBudget kShippingBudget{
    .rt = kRtStopDeadlineNs,
    .io = kIoStopDeadlineNs
};
}

void ThreadAssembly::stop_join_or_exit() noexcept {
    stop_join_or_exit(kShippingBudget);
}

void ThreadAssembly::stop_join_or_exit(StopBudget budget) noexcept {
    if (!any_live())
        return;
    stop();
    (void)join(budget);
    if (any_live())
        exit_seats_live_();
}

void ThreadAssembly::exit_seats_live_() const noexcept {
    for (const SeatTag s : kJoinOrder) {
        if (!seats_[seat_index(s)].live)
            continue;
        infra::JsonOut out;
        to_json(
            out,
            WedgeRecord{.seat = s, .tid = tid(s), .at = WedgeAt::Teardown, .exit = ExitCode::Wedged}
        );

        const std::string_view line = out.take_line();
        const auto w = ::write(STDERR_FILENO, line.data(), line.size());
        (void)w;
    }
    ::_exit(kRtWedgeExitStatus);
}

DiagSampler::Sources ThreadAssembly::with_ui_cells_(
    DiagSampler::Sources s,
    const UiMain::Wiring& ui
) noexcept {
    return DiagSampler::Sources{
        .fifo = infra::opt_ref_of(ui.fifo ? &ui.fifo->stats_cell() : nullptr),
        .mgl = infra::opt_ref_of(ui.mgl ? &ui.mgl->stats_cell() : nullptr),
        .device = s.device,
        .video_stats = ui.video.stats_cell(),
        .video_geometry = ui.video.geometry_cell(),
        .video_wire = ui.video.wire(),
        .prelude_stats = ui.video.prelude().stats_cell(),
        .ui_pages = s.ui_pages,
        .hd = s.hd,
        .replay = s.replay,
        .rec_capture = s.rec_capture,
        .rec_encode = s.rec_encode,
        .rec_write = s.rec_write,
        .rec_avi = s.rec_avi,
    };
}

std::array<char, hal::kCommNameMax + 1> ThreadAssembly::process_comm_() noexcept {
    std::array<char, hal::kCommNameMax + 1> out{};
    if (::prctl(PR_GET_NAME, out.data(), 0, 0, 0) != 0)
        out.fill('\0');
    return out;
}

SeatBirth ThreadAssembly::adopt_seat(SeatTag s) noexcept {
    block_process_stop_set();
    const hal::ThreadRole& r = hal::seat_of(threads_, s);

    const hal::CommName comm = hal::comm_name(comm_prefix_.data(), seat_role(s));
    const int nrc = ::pthread_setname_np(::pthread_self(), comm.text.data());
    SeatBirth birth{};
    birth.name = RtSetup{nrc == 0, nrc};

    if (r.spawn == hal::SpawnKind::PromoteInPlace || !infra::kPosixGlibc) {
        birth.affinity = pin_current_cpu(r.cpu);
    } else {
        birth.affinity = confirm_current_cpu(r.cpu);
    }

    hal::adopt_placement(threads_, s);
    (void)adopt_seat_tag(s);
    return birth;
}

void ThreadAssembly::collect_births_() noexcept {
    for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
        if (!seats_[i].live)
            continue;
        if (const auto birth = seats_[i].birth.sample(); birth) {
            ev_.seats[i].name = birth.value.name;
            ev_.seats[i].affinity = birth.value.affinity;
        }
    }
}

template <class Row>
void ThreadAssembly::stop_one_() noexcept {
    if (auto* m = mains_.get<typename Row::Main>(); m != nullptr)
        m->stop();
}

template <class... Rows>
void ThreadAssembly::stop_seat_(SeatTag s, SeatList<Rows...>) noexcept {
    ((Rows::seat == s ? stop_one_<Rows>() : void()), ...);
}

void ThreadAssembly::stop_bound_(SeatTag s) noexcept {
    stop_seat_(s, SeatMainList{});
}

template <class Row>
void* ThreadAssembly::trampoline(void* self) {
    using M = typename Row::Main;
    static_assert(xthread::SeatBody<M>);
    constexpr SeatTag s = Row::seat;
    auto* a = static_cast<ThreadAssembly*>(self);
    const SeatBirth birth = a->adopt_seat(s);
    a->seats_[seat_index(s)].birth.publish(birth);
    a->seats_[seat_index(s)].tid.store(os::KernelTid::current(), std::memory_order_release);
    a->mains_.get<M>()->start();
    if constexpr (s == SeatTag::RT) {

        RtOutcome outcome{};
        if (const Ex<void> result = a->mains_.get<M>()->result(); !result) {
            outcome.failure = result.error();
        }
        a->rt_outcome_.publish(outcome);
        a->rt_exited_.store(true, std::memory_order_release);
        a->main_wake_.request();
    }
    if constexpr (s == SeatTag::Io) {
        a->io_exited_.store(true, std::memory_order_release);
    }
    return nullptr;
}

Ex<void> ThreadAssembly::check_presence_() const noexcept {
    const SeatTag missing = first_absent_seat(
        [this](SeatTag s) noexcept { return mains_.bound(s); },
        contract_
    );
    if (missing == SeatTag::Unbound)
        return {};
    return std::unexpected(Error{Errc::seat_absent, ERR_SITE(), seat::detail::tag_detail(missing)});
}

Ex<void> ThreadAssembly::spawn(RtMode mode, const SeatMains& mains) {

    app::RtMain* const rt = mains_.get<app::RtMain>();
    mains_ = mains;
    if (!born_)
        return born_;
    mains_.bind(&*diag_main_);
    mains_.bind(&*ui_main_);
    mains_.bind(rt);

    if (auto present = check_presence_(); !present)
        return present;

    publish_evidence_();

    for (const SeatTag s : std::span(kSpawnOrder).first(kSpawnOrder.size() - 1)) {
        if (mains_.bound(s)) {
            const std::size_t i = seat_index(s);
            SpawnOutcome
                spawned = spawn_seat(hal::seat_of(threads_, s), SeatTable::of(s).body, this, mode);
            ev_.seats[i].sched = spawned.sched;
            if (!spawned.handle)
                return std::unexpected(spawned.handle.error());
            seats_[i].handle = *spawned.handle;
            seats_[i].live = true;
        }
    }

    if (auto p = await_published([this] {
            for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
                const os::KernelTid published = seats_[i].tid.load(std::memory_order_acquire);
                if (seats_[i].live && published == kNoTidPublished)
                    return false;
            }
            return true;
        });
        !p) {
        return p;
    }
    collect_births_();
    publish_evidence_();
    return {};
}

Ex<void> ThreadAssembly::spawn_rt(RtMode mode, app::RtMain& rt) {

    mains_.bind(&rt);
    const std::size_t i = seat_index(SeatTag::RT);

    if (auto s = os::set_timer_slack(os::kMinTimerSlack); !s) {
        return std::unexpected(s.error());
    }
    SpawnOutcome spawned = spawn_seat(
        hal::seat_of(threads_, SeatTag::RT),
        SeatTable::of(SeatTag::RT).body,
        this,
        mode
    );
    [[maybe_unused]] Ex<void> restored = os::restore_default_timer_slack();
    ev_.seats[i].sched = spawned.sched;
    if (!spawned.handle)
        return std::unexpected(spawned.handle.error());
    seats_[i].handle = *spawned.handle;
    seats_[i].live = true;
    if (auto p = await_published([this] { return tid(SeatTag::RT).has_value(); }); !p)
        return p;
    collect_births_();
    publish_evidence_();
    return {};
}

namespace {

int join_within(
    pthread_t thread,
    [[maybe_unused]] const std::atomic<bool>& exited,
    const timespec& abs
) noexcept {
#if defined(__GLIBC__)
    return ::pthread_clockjoin_np(thread, nullptr, CLOCK_MONOTONIC, &abs);
#else
    for (;;) {
        if (exited.load(std::memory_order_acquire))
            return ::pthread_join(thread, nullptr);
        timespec now{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > abs.tv_sec || (now.tv_sec == abs.tv_sec && now.tv_nsec >= abs.tv_nsec)) {
            return ETIMEDOUT;
        }
        timespec slp{};
        constexpr std::chrono::milliseconds kJoinPoll{1};
        slp.tv_nsec = static_cast<long>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(kJoinPoll).count()
        );
        (void)::clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, nullptr);
    }
#endif
}

timespec deadline_from_now(std::chrono::nanoseconds deadline) noexcept {
    timespec abs{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &abs);
    const std::int64_t total = static_cast<std::int64_t>(abs.tv_nsec) + deadline.count();
    abs.tv_sec += static_cast<time_t>(total / kOneSecond.count());
    abs.tv_nsec = static_cast<long>(total % kOneSecond.count());
    return abs;
}

}

Ex<void> ThreadAssembly::join_rt_within(std::chrono::nanoseconds deadline) {
    const std::size_t i = seat_index(SeatTag::RT);
    if (!seats_[i].live)
        return {};
    const int rc = join_within(seats_[i].handle, rt_exited_, deadline_from_now(deadline));
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{
                Errc::timeout,
                ERR_SITE(),
                static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline).count()
                )
            }
        );
    }
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc));
    seats_[i].live = false;
    return {};
}

Ex<void> ThreadAssembly::join_io_within(std::chrono::nanoseconds deadline) {
    const std::size_t i = seat_index(SeatTag::Io);
    if (!seats_[i].live)
        return {};
    const int rc = join_within(seats_[i].handle, io_exited_, deadline_from_now(deadline));
    if (rc == ETIMEDOUT) {
        return std::unexpected(
            Error{
                Errc::timeout,
                ERR_SITE(),
                static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline).count()
                )
            }
        );
    }
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc));
    seats_[i].live = false;
    return {};
}

Ex<void> ThreadAssembly::stop_and_join_rt(std::chrono::nanoseconds deadline) {
    if (!seats_[seat_index(SeatTag::RT)].live)
        return {};
    const auto hard = std::chrono::steady_clock::now() + deadline;
    for (;;) {

        mains_.get<app::RtMain>()->stop();
        std::chrono::nanoseconds rem = hard - std::chrono::steady_clock::now();
        if (rem <= std::chrono::nanoseconds::zero())
            return join_rt_within(std::chrono::nanoseconds::zero());
        if (rem > kRtJoinSliceNs)
            rem = kRtJoinSliceNs;
        if (auto r = join_rt_within(rem); r)
            return r;
        else if (r.error().code != Errc::timeout)
            return r;
    }
}

void ThreadAssembly::stop() noexcept {

    for (const SeatTag s : kStopOrder) {
        if (s == SeatTag::RT && !seats_[seat_index(s)].live)
            continue;
        stop_bound_(s);
    }
}

void ThreadAssembly::mark_quiescing() noexcept {

    quiescing_.request();
}

Ex<void> ThreadAssembly::join() {
    return join(kShippingBudget);
}

Ex<void> ThreadAssembly::join(StopBudget budget) {

    static_assert(kJoinOrder[0] == SeatTag::RT && kJoinOrder[1] == SeatTag::Io);
    if (seats_[seat_index(SeatTag::RT)].live) {

        if (auto r = stop_and_join_rt(budget.rt); !r)
            return r;
    }

    Ex<void> err{};
    if (seats_[seat_index(SeatTag::Io)].live && !io_poisoned_) {
        if (auto r = join_io_within(budget.io); !r) {
            if (r.error().code == Errc::timeout) {

                io_poisoned_ = true;
            }
            err = std::unexpected(r.error());
        }
    }

    for (const SeatTag s : std::span(kJoinOrder).subspan(2)) {
        if (drains_after_join(kStopAfterJoinOf, s))
            stop_bound_(s);
        const std::size_t i = seat_index(s);
        if (!seats_[i].live)
            continue;
        if (const int rc = ::pthread_join(seats_[i].handle, nullptr); rc != 0) {
            if (err)
                err = std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc));
            continue;
        }
        seats_[i].live = false;
    }

    return err;
}

}
