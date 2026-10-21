// SPDX-License-Identifier: GPL-3.0-or-later
#include "process_standup.h"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "assembly.h"
#include "board_parts.h"
#include "census.h"
#include "hub.h"
#include "session_mode.h"
#include "process_main.h"
#include "report.h"
#include "app/board_ops.h"
#include "app/board_reboot.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/persist.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/boards_table.h"
#include "reactor/executive.h"
#include "hal/irq_pin.h"
#include "hal/link_port.h"
#include "hal/link_timing.h"
#include "hal/program_geometry.h"
#include "svc/vfs.h"

using mister::fw::report;

namespace {

using namespace mister;

std::atomic<int> g_stop_requested TASTY_PERSIST(proc, main_stop_requested){0};
static_assert(std::atomic<int>::is_always_lock_free, "[support.signal]: a handler stores it");

std::atomic<fw::ProcessMain*> g_process_main TASTY_PERSIST(proc, main_process_main){nullptr};

extern "C" void on_stop_signal(int) {
    (void)fw::ProcessStandup::latch_stop();
}

alignas(hal::BoardWindows) unsigned char g_windows_storage[sizeof(hal::BoardWindows)] TASTY_PERSIST(
    proc,
    main_board_windows
);
static_assert(
    std::is_trivially_destructible_v<decltype(g_windows_storage)>,
    "the board windows must have no static destructor (mmio.cpp:65-77)"
);

constinit xthread::RtStats stats TASTY_PERSIST(proc, main_rt_stats){};

constinit reactor::RoundTimer round_timer TASTY_PERSIST(proc, main_round_timer){};
constinit xthread::LogLane rt_lane TASTY_PERSIST(proc, main_rt_lane){};

[[noreturn]] void exit_io_wedged(const fw::ThreadAssembly& assembly, const fw::SessionMode& mode) {
    char line[96];
    const int n = std::snprintf(
        line,
        sizeof line,
        "{\"t\":\"wedge\",\"seat\":\"T-IO\",\"tid\":%ld}\n",
        assembly.tid(SeatTag::Io)
    );
    if (n > 0)
        mode.voice().wedged(std::string_view(line, static_cast<std::size_t>(n)));
    ::_exit(fw::kRtWedgeExitStatus);
}

[[noreturn]] void exit_wedged(
    fw::ThreadAssembly& assembly,
    const xthread::RtStats& rt_stats,
    const Error& why,
    const fw::SessionMode& mode
) {
    char state = '?';
    const char* wchan = "?";
    int policy = -1;
    int prio = -1;
    const auto sched = fw::read_thread_sched(assembly.tid(SeatTag::RT));
    if (sched) {
        state = sched->state;
        wchan = sched->wchan.size() != 0 ? sched->wchan.c_str() : "-";
        policy = sched->policy;
        prio = sched->rt_priority;
    }
    const auto hb = static_cast<unsigned long long>(
        rt_stats.heartbeat.load(std::memory_order_relaxed)
    );
    const auto deadline_ms = static_cast<long long>(fw::kRtStopDeadlineNs / 1'000'000);
    assembly.diag().diag_sink().appendf(
        "{\"t\":\"wedge\",\"seat\":\"T-RT\",\"tid\":%ld,\"state\":\"%c\",\"wchan\":\"%s\","
        "\"policy\":%d,\"prio\":%d,\"hb\":%llu,\"deadline_ms\":%lld,\"err\":%u,\"site\":%u,"
        "\"exit\":%d}",
        assembly.tid(SeatTag::RT),
        state,
        wchan,
        policy,
        prio,
        hb,
        deadline_ms,
        static_cast<unsigned>(why.code),
        static_cast<unsigned>(why.site),
        fw::kRtWedgeExitStatus
    );
    char line[256];
    const int n = std::snprintf(
        line,
        sizeof line,
        "%s: T-RT wedged in %c-state on %s, %lld ms after a stop "
        "(heartbeat %llu); teardown SKIPPED, exit %d\n",
        mode.traits().program,
        state,
        wchan,
        deadline_ms,
        hb,
        fw::kRtWedgeExitStatus
    );
    if (n > 0)
        mode.voice().wedged(std::string_view(line, static_cast<std::size_t>(n)));
    ::_exit(fw::kRtWedgeExitStatus);
}

}

namespace mister::fw {

ProcessStandup::StopSignals::StopSignals() noexcept {
    struct sigaction sa{};
    sa.sa_handler = &on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    (void)::sigaction(SIGINT, &sa, nullptr);
    (void)::sigaction(SIGTERM, &sa, nullptr);

    (void)::sigaction(SIGHUP, &sa, nullptr);
    (void)::sigaction(SIGQUIT, &sa, nullptr);
}

ProcessStandup::StopSignals::~StopSignals() {
    disarm();
}

void ProcessStandup::StopSignals::disarm() noexcept {
    if (!armed_)
        return;
    armed_ = false;
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    (void)::sigaction(SIGINT, &sa, nullptr);
    (void)::sigaction(SIGTERM, &sa, nullptr);
    (void)::sigaction(SIGHUP, &sa, nullptr);
    (void)::sigaction(SIGQUIT, &sa, nullptr);
}

bool ProcessStandup::latch_stop() noexcept {
    const int prev = g_stop_requested.exchange(1, std::memory_order_seq_cst);
    if (fw::ProcessMain* m = g_process_main.load(std::memory_order_seq_cst)) {
        m->stop();
    }
    return prev == 0;
}

const std::atomic<int>& ProcessStandup::stop_latch() noexcept {
    return g_stop_requested;
}

int ProcessStandup::ExitPath::fail() const {
    if (!link_)
        return 1;
    if (session_) {
        app::LinkSession& sup = session_->sup_;
        app::ExitReboot owed = app::ExitReboot::None;
        {
            const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
            if (auto sd = sup.shutdown(); !sd) {
                report("LinkSession::shutdown", sd.error());
            } else {
                owed = *sd;
            }
        }
        if (owed == app::ExitReboot::Cold)
            session_->reboot_at_exit();
        {

            const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
            if (session_->io_ == IoSeat::Stopped)
                sup.settle_writes();
            session_->warn_if_unconfigured();
        }
    } else {
        fw::release_link(*link_);
    }
    return 1;
}

const ProcessStandup::ExitPath::SessionStage& ProcessStandup::ExitPath::session() const {
    if (!session_) {
        fatal(
            Error{Errc::not_found, ERR_SITE(), 0},
            "ExitPath::session() before the session stage"
        );
    }
    return *session_;
}

void ProcessStandup::ExitPath::SessionStage::reboot_at_exit() const {
    {

        const mister::SeatScope main_as_main{mister::SeatTag::Main};
        if (auto r = reboot_.get().reboot(true); !r)
            report("BoardReboot::reboot", r.error());
    }
    const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
    sup_.get().release_bus();
}

void ProcessStandup::ExitPath::SessionStage::warn_if_unconfigured() const {
    mode_.get().voice().unconfigured(sup_.get());
}

bool ProcessStandup::MainStopSignal::stop_requested() const {
    return g_stop_requested.load() != 0;
}

ProcessStandup::ProcessStandup(SessionMode& mode) noexcept : mode_(mode) {}

ProcessStandup::~ProcessStandup() = default;

int ProcessStandup::run() {

    mode_.title_process();
    if (const auto rc = mode_.prepare())
        return *rc;
    if (const auto rc = prelude())
        return *rc;
    if (const auto rc = identify_board())
        return *rc;
    if (const auto rc = open_platform())
        return *rc;
    if (const auto rc = gate_fabric())
        return *rc;
    if (const auto rc = open_storage())
        return *rc;
    if (const auto rc = assemble_hub())
        return *rc;
    if (const auto rc = run_boot_ladder())
        return *rc;
    if (const auto rc = spawn_seats())
        return *rc;
    const auto ran = supervise();
    return shut_down(ran);
}

ProcessStandup::Exit ProcessStandup::prelude() noexcept {

    if (auto r = fw::rt_memory_init(fw::RtMode::Required, ev_); !r) {
        return mode_.refused(r.error(), {"mlockall(MCL_CURRENT|MCL_FUTURE)", " — arch §6"});
    }

    fw::warn_vm_compaction(ev_.vm_compaction, mode_.traits().program, stderr);

    fw::prefault_current_stack(hal::kFifoStackBytes, ev_);
    return {};
}

ProcessStandup::Exit ProcessStandup::identify_board() {

    auto compatible = hal::read_compatible(hal::kCompatiblePath);
    if (!compatible)
        return mode_.refused(compatible.error(), {hal::kCompatiblePath});
    auto selected = hal::select_board(*compatible);
    if (!selected) {
        mode_.voice().unknown_board(*compatible);
        return mode_.refused(selected.error(), {"no board profile claims this device tree"});
    }
    const hal::BoardProfile& board = **selected;
    board_ = infra::OptRef<const hal::BoardProfile>{board};
    mode_.voice().board(board, *compatible);

    if (auto t = fw::rt_topology_init(board.threads, fw::RtMode::Required, ev_); !t) {
        return mode_.refused(
            t.error(),
            {"the board's thread map names a CPU this board does "
             "not have (sysconf(_SC_NPROCESSORS_ONLN))",
             " — item"}
        );
    }

    fw::pin_current_cpu(board.main_cpu, ev_.main_affinity);

    if (auto c = fw::verify_census(); !c) {
        return mode_.refused(c.error(), {"LinkDecoderDecl census drift", " (arch §2)"});
    }

    {
        const auto pinned = hal::pin_irq_affinities(board.irq_pins, board.threads, hal::kProcRoot);
        mode_.voice().irq_pins(pinned);
        ev_.irq_affinity = fw::RtSetup{pinned.all_applied, pinned.first_err};
    }
    return {};
}

ProcessStandup::Exit ProcessStandup::open_platform() {
    const hal::BoardProfile& board = *board_;

    auto opened_windows = hal::BoardWindows::open(board, mode_.traits().windows_notice);
    if (!opened_windows) {
        report("BoardWindows::open", opened_windows.error());
        return exit_.fail();
    }
    hal::BoardWindows& windows = *(new (static_cast<void*>(g_windows_storage))
                                       hal::BoardWindows(std::move(*opened_windows)));

    auto parts = fw::BoardParts::make(board, windows, stats);
    if (!parts) {
        report(parts.error().role, parts.error().why);
        return exit_.fail();
    }
    parts_.emplace(std::move(*parts));

    hal::ILinkPort& link = parts_->link();
    exit_ = ExitPath{link};

    auto exec = reactor::Executive::create(
        stats,
        {.round_timer = infra::OptRef<reactor::RoundTimer>{round_timer},
         .log_lane = infra::OptRef<xthread::LogLane>{rt_lane}}
    );
    if (!exec) {
        report("Executive::create", exec.error());
        return exit_.fail();
    }
    exec_.emplace(std::move(*exec));

    mode_.arm_stop_signals();
    return {};
}

ProcessStandup::Exit ProcessStandup::gate_fabric() {
    const hal::BoardProfile& board = *board_;
    hal::ILinkPort& link = parts_->link();

    const bool fabric_ready = [&link] {
        const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
        return link.ready();
    }();
    if (!fabric_ready) {

        mode_.voice().fabric_not_ready(board);
        if (board.verified && mode_.traits().not_ready_quits_clean)
            std::exit(0);
        return exit_.fail();
    }
    return {};
}

ProcessStandup::Exit ProcessStandup::open_storage() {

    auto vfs = svc::Vfs::create(svc::StorageRoot{});
    if (!vfs) {
        report("Vfs::create", vfs.error());
        return exit_.fail();
    }
    vfs_.emplace(std::move(*vfs));
    mode_.storage_ready(*vfs_);
    return {};
}

ProcessStandup::Exit ProcessStandup::assemble_hub() {
    const hal::BoardProfile& board = *board_;
    hal::ILinkPort& link = parts_->link();

    const auto link_timing = hal::LinkTimingValues::resolve(board.timing, ERR_SITE());
    if (!link_timing) {
        report("LinkTiming", link_timing.error());
        return exit_.fail();
    }
    const auto program = hal::ProgramGeometryValues::resolve(board.program, ERR_SITE());
    if (!program) {
        report("ProgramGeometry", program.error());
        return exit_.fail();
    }

    boot_parts_.emplace(
        fw::BootParts{
            .link = link,
            .bridges = parts_->bridges(),
            .programmer = parts_->programmer(),
            .fpga_mem = board.fpga_mem,
            .lw_window = hal::lw_region(board),
            .regions = board.regions,
            .doorbells = board.doorbells,
            .doorbell_nodes = hal::doorbell_nodes(board),
            .video = board.video,
            .kernel = board.kernel,
            .threads = board.threads,
            .exec = *exec_,
            .stats = stats,
            .round_timer = round_timer,
            .rt_lane = rt_lane,
            .vfs = *vfs_,
            .rt_evidence = ev_,
            .kernel_rt = ev_.kernel,
            .link_timing = *link_timing,
            .program = *program,
        }
    );

    static_assert(__cpp_aligned_new >= 201606L);
    hub_ = std::make_unique<fw::Hub>(*boot_parts_, stop_signal_, mode_);
    app::LinkSession& session = hub_->link_session();
    app::SessionOwner& owner = hub_->owner();

    exit_ = ExitPath{link, session, owner.board_reboot(), IoSeat::Stopped, mode_};
    mode_.voice().kernel(ev_.kernel);
    return {};
}

ProcessStandup::Exit ProcessStandup::run_boot_ladder() {
    app::LinkSession& session = hub_->link_session();
    app::SessionOwner& owner = hub_->owner();
    {

        const mister::SeatScope boot_stands_in_for_rt{mister::SeatTag::RT};

        const std::span<const char* const> boot_args = mode_.traits().boot_args;

        mode_.before_boot(session);
        const auto b = session.boot(boot_args);
        if (!b) {
            report("LinkSession::boot", b.error());
            return exit_.fail();
        }

        {

            const mister::SeatScope main_as_main{mister::SeatTag::Main};
            owner.latch_boot_companion(boot_args.size() > 2 ? boot_args[2] : "");
            if (*b)
                owner.service_boot(**b);
            else
                owner.expect_boot_negotiate();
        }

        bool boot_config_latched = false;
        for (;;) {
            {

                const mister::SeatScope main_as_main{mister::SeatTag::Main};
                fw::ProcessMain::pass(owner);

                if (!boot_config_latched && owner.boot_config_parsed()) {
                    boot_config_latched = true;
                    if (const auto exe = mode_.boot_handoff(owner)) {
                        app::ExitReboot failed = app::ExitReboot::None;
                        {
                            const mister::SeatScope session_as_rt{mister::SeatTag::RT};
                            failed = session.handoff_to_alternate_executable(
                                exe->c_str(),
                                boot_args
                            );
                        }

                        if (failed == app::ExitReboot::Cold) {
                            exit_.session().reboot_at_exit();
                        }
                        ::_exit(1);
                    }
                    hub_->latch_boot_config();
                }
            }

            if (g_stop_requested.load(std::memory_order_seq_cst) != 0
                && mode_.traits().stop_ends_ladder) {
                hub_->main_stop().request();
                break;
            }
            auto r = hub_->rt_main().pump_boot();
            if (r)
                break;

            if (r.error().code == Errc::cancelled) {
                report("RtMain::pump_boot (stop requested)", r.error());
                return exit_.fail();
            }
            if (r.error().code == Errc::would_block)
                continue;

            report("RtMain::pump_boot", r.error());
            break;
        }

        if (!boot_config_latched)
            hub_->latch_boot_config();
        if (session.state() == app::SessionState::Failed) {

            return exit_.fail();
        }
    }
    return {};
}

ProcessStandup::Exit ProcessStandup::spawn_seats() {
    app::LinkSession& session = hub_->link_session();
    app::SessionOwner& owner = hub_->owner();
    fw::ThreadAssembly& assembly = hub_->threads();
    hal::ILinkPort& link = parts_->link();

    if (auto s = assembly.spawn(fw::RtMode::Required, hub_->seat_mains()); !s) {
        report("ThreadAssembly::spawn", s.error());
        assembly.stop();
        if (auto j = assembly.join(); !j)
            report("ThreadAssembly::join", j.error());
        if (assembly.io_wedged())
            exit_io_wedged(assembly, mode_);
        return exit_.fail();
    }

    exit_ = ExitPath{link, session, owner.board_reboot(), IoSeat::Running, mode_};

    if (assembly.has_io_seat())
        session.seal_boot_pump();

    const char* rt_step = "ProcessMain::open";
    Ex<void> rt_up = hub_->open_process_main();
    if (rt_up) {
        rt_step = "ThreadAssembly::spawn_rt(FIFO 40)";
        rt_up = assembly.spawn_rt(fw::RtMode::Required, hub_->rt_main());
    }
    if (!rt_up) {
        report(rt_step, rt_up.error());

        assembly.mark_quiescing();
        const int rc = exit_.fail();
        assembly.stop();
        if (auto j = assembly.join(); !j)
            report("ThreadAssembly::join", j.error());
        if (assembly.io_wedged())
            exit_io_wedged(assembly, mode_);
        return rc;
    }
    return {};
}

Ex<void> ProcessStandup::supervise() {
    fw::ThreadAssembly& assembly = hub_->threads();
    fw::ProcessMain& process_main = *hub_->process_main();

    g_process_main.store(&process_main, std::memory_order_seq_cst);
    if (g_stop_requested.load(std::memory_order_seq_cst) != 0)
        process_main.stop();

    process_main.start();
    if (auto j = assembly.stop_and_join_rt(fw::kRtStopDeadlineNs); !j) {
        exit_wedged(assembly, stats, j.error(), mode_);
    }
    return assembly.rt_result();
}

int ProcessStandup::shut_down(const Ex<void>& ran) {
    app::LinkSession& session = hub_->link_session();
    fw::ThreadAssembly& assembly = hub_->threads();

    mode_.disarm_stop_signals();
    g_process_main.store(nullptr, std::memory_order_release);

    app::ExitReboot exit_owed = app::ExitReboot::None;
    {

        const mister::SeatScope shutdown_stands_in_for_rt{mister::SeatTag::RT};

        assembly.mark_quiescing();

        if (auto sd = session.shutdown(); !sd) {
            report("LinkSession::shutdown", sd.error());
        } else {
            exit_owed = *sd;
        }
    }
    if (exit_owed == app::ExitReboot::Cold)
        exit_.session().reboot_at_exit();

    exit_.session().warn_if_unconfigured();
    assembly.stop();
    if (auto j = assembly.join(); !j) {
        report("ThreadAssembly::join", j.error());
    }
    if (assembly.io_wedged())
        exit_io_wedged(assembly, mode_);
    if (!ran)
        report("RtMain::start", ran.error());

    return mode_.exit_status(ran);
}

}
