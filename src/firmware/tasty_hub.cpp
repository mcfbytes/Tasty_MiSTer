// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_hub.h"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "assembly.h"
#include "board_parts.h"
#include "census.h"
#include "hub.h"
#include "process_main.h"
#include "app/board_ops.h"
#include "hal/board_profile.h"
#include "hal/thread_map.h"
#include "hal/board_windows.h"
#include "hal/boards_table.h"
#include "hal/irq_pin.h"
#include "hal/link_port.h"
#include "hal/link_timing.h"
#include "hal/program_geometry.h"
#include "hal/region_id.h"
#include "infra/error.h"
#include "infra/persist.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "rt_evidence.h"
#include "rt_setup.h"
#include "reactor/executive.h"
#include "svc/vfs.h"
#include "app/replay_feeder.h"
#include "cores/movie_codec.h"
#include "tasty_ctl.h"
#include "tasty_session.h"

using mister::fw::report;
extern char** environ;

namespace {

using namespace mister;

std::atomic<int> g_stop_requested TASTY_PERSIST(proc, tasty_stop_requested){0};

enum class HomeLane : int { Idle = 0, Open = 1, Stopping = 2, Left = 3 };
std::atomic<int> g_home_owed TASTY_PERSIST(proc, tasty_home_owed){0};
std::atomic<int> g_deferred_stop TASTY_PERSIST(proc, tasty_deferred_stop){0};
static_assert(std::atomic<int>::is_always_lock_free);

std::atomic<int> g_exit_code TASTY_PERSIST(proc, tasty_exit_code){0};
std::atomic<fw::ProcessMain*> g_process_main TASTY_PERSIST(proc, tasty_process_main){nullptr};
static_assert(std::atomic<fw::ProcessMain*>::is_always_lock_free);

alignas(16) unsigned char g_fatal_stack[32 * 1024] TASTY_PERSIST(proc, tasty_fatal_stack){};

extern "C" void on_tasty_stop(int sig) {
    const int prev = g_stop_requested.exchange(1, std::memory_order_seq_cst);
    if (prev == 0) {
        if (fw::ProcessMain* m = g_process_main.load(std::memory_order_seq_cst)) m->stop();
        return;
    }
    const int lane = g_home_owed.load(std::memory_order_seq_cst);
    if (lane == static_cast<int>(HomeLane::Open)) {
        (void)fw::tasty_launch_home();
        ::_exit(128 + sig);
    }

    if (lane == static_cast<int>(HomeLane::Stopping))
        g_deferred_stop.store(sig, std::memory_order_seq_cst);
}

extern "C" void on_tasty_fatal(int sig) {
    if (g_home_owed.load(std::memory_order_seq_cst) == static_cast<int>(HomeLane::Open))
        (void)fw::tasty_launch_home();
    sigset_t set;
    ::sigemptyset(&set);
    ::sigaddset(&set, sig);
    (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
    struct sigaction sa {};
    sa.sa_handler = SIG_DFL;
    ::sigemptyset(&sa.sa_mask);
    (void)::sigaction(sig, &sa, nullptr);
    (void)::raise(sig);
    ::_exit(128 + sig);
}

struct FeederReplay final : fw::TastySession::Replay {
    TASTY_SEAT_RESIDENT(Ui);
    app::ReplayFeeder* f = nullptr;
    bool take_play(const fw::TastySession::PlayAsk& ask) noexcept override {
        TASTY_SEAT_BODY(FeederReplay);
        if (f == nullptr) return false;
        app::ReplayFeeder::Play p{};
        p.movie = ask.movie;
        p.rom = ask.rom;
        p.phase_us = ask.phase_us;
        p.lead = ask.lead;
        p.stop_at = ask.stop_at;
        p.ram_fill = ask.ram_fill;
        p.seeded_save = ask.seeded_save;
        p.set_settings = ask.set_settings;
        (void)f->take_play(p);
        return f->last_refusal() == app::ReplayFeeder::Refusal::None;
    }
    void take_stop() noexcept override {
        TASTY_SEAT_BODY(FeederReplay);
        if (f != nullptr) (void)f->take_stop();
    }
    std::uint32_t frames() const noexcept override { return f != nullptr ? f->frames() : 0; }
    std::uint32_t movie_frames() const noexcept override {
        return f != nullptr ? f->movie_frames() : 0;
    }
    bool refused() const noexcept override {
        return f != nullptr && f->last_refusal() != app::ReplayFeeder::Refusal::None;
    }
    bool active() const noexcept override {
        return f != nullptr && f->stage() != app::ReplayFeeder::Stage::Idle;
    }
    const char* refusal_why() const noexcept override {
        if (f == nullptr) return "none";
        if (f->last_refusal() == app::ReplayFeeder::Refusal::Movie)
            return fw::tasty_movie_refusal_token(f->last_refusal_detail());
        return app::ReplayFeeder::refusal_name(f->last_refusal());
    }
    std::string_view refusal_setting() const noexcept override {
        return f != nullptr ? f->last_setting() : std::string_view{};
    }
    bool refusal_setting_offered() const noexcept override {
        return f != nullptr && f->last_setting_offered();
    }
    std::string_view refusal_path() const noexcept override {
        return f != nullptr ? f->refusal_path() : std::string_view{};
    }
    std::size_t settings_set() const noexcept override {
        return f != nullptr ? f->settings_set().size() : 0;
    }
    std::string setting_line(std::size_t i) const override {
        if (f == nullptr || i >= f->settings_set().size()) return {};
        const app::ReplayFeeder::SetSetting& s = f->settings_set()[i];
        return std::string(s.name) + ": using " + s.now + " for this run (your setting: " + s.was +
               ")";
    }
};

struct MainStopSignal final : app::IStopSignal {
    bool stop_requested() const override { return g_stop_requested.load() != 0; }
};

alignas(hal::BoardWindows) unsigned char g_windows_storage[sizeof(hal::BoardWindows)] TASTY_PERSIST(
    proc, tasty_board_windows);
static_assert(std::is_trivially_destructible_v<decltype(g_windows_storage)>);

void announce_board(const hal::BoardProfile& board, const hal::CompatibleBlob& blob) {
    for (const std::string_view s : board.compatible) {
        if (!blob.contains(s)) continue;
        char buf[128];
        std::snprintf(buf, sizeof buf, "tasty: board %.*s", static_cast<int>(board.model.size()),
                      board.model.data());
        fw::tasty_say(buf);
        break;
    }
    (void)blob;
}

void report_unknown_board(const hal::CompatibleBlob&) {
    fw::tasty_say("tasty: no board profile claims this device tree");
}

enum class IoSeat : std::uint8_t { Stopped, Running };

int teardown(int rc, hal::ILinkPort& link, fw::Hub* hub, IoSeat io) {
    if (hub != nullptr) {
        const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
        if (auto sd = hub->shutdown_link(); !sd) report("LinkSession::shutdown", sd.error());
        if (io == IoSeat::Stopped) hub->settle_writes();
    } else {
        fw::release_link(link);
    }
    return rc;
}

[[noreturn]] void exit_io_wedged(const fw::ThreadAssembly& assembly) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "{\"t\":\"wedge\",\"seat\":\"T-IO\",\"tid\":%ld}",
                  assembly.io_tid());
    fw::tasty_say(buf);
    (void)fw::tasty_launch_home();
    ::_exit(fw::kRtWedgeExitStatus);
}

[[noreturn]] void exit_wedged(fw::ThreadAssembly& assembly, const Error& why) {
    (void)assembly;
    char buf[96];
    std::snprintf(buf, sizeof buf, "tasty: T-RT wedged err=%u site=%u",
                  static_cast<unsigned>(why.code), static_cast<unsigned>(why.site));
    fw::tasty_say(buf);
    (void)fw::tasty_launch_home();
    ::_exit(fw::kRtWedgeExitStatus);
}

}  // namespace

const std::atomic<int>& mister::fw::tasty_owner_stop_flag() noexcept { return g_stop_requested; }

void mister::fw::tasty_owe_home() noexcept {
    g_home_owed.store(static_cast<int>(HomeLane::Open), std::memory_order_seq_cst);
}

bool mister::fw::tasty_home_may_launch() noexcept {
    const int lane = g_home_owed.load(std::memory_order_seq_cst);
    return lane != static_cast<int>(HomeLane::Stopping) && lane != static_cast<int>(HomeLane::Left);
}

int mister::fw::tasty_stop_and_owe() noexcept {
    g_deferred_stop.store(0, std::memory_order_seq_cst);
    g_home_owed.store(static_cast<int>(HomeLane::Stopping), std::memory_order_seq_cst);
    bool gone = false;
    if (const auto stopped = tasty_stop_stock()) gone = *stopped;
    if (!gone) {
        g_home_owed.store(static_cast<int>(HomeLane::Left), std::memory_order_seq_cst);
        tasty_say("tasty: the menu was left running");
        return 1;
    }
    tasty_owe_home();
    if (const int sig = g_deferred_stop.exchange(0, std::memory_order_seq_cst)) {
        (void)tasty_launch_home();
        ::_exit(128 + sig);
    }
    return 0;
}

void mister::fw::tasty_arm_owner_signals() noexcept {
    stack_t ss{};
    ss.ss_sp = g_fatal_stack;
    ss.ss_size = sizeof g_fatal_stack;
    ss.ss_flags = 0;
    (void)::sigaltstack(&ss, nullptr);

    struct sigaction ign {};
    ign.sa_handler = SIG_IGN;
    ::sigemptyset(&ign.sa_mask);
    (void)::sigaction(SIGHUP, &ign, nullptr);
    (void)::sigaction(SIGPIPE, &ign, nullptr);

    struct sigaction sa {};
    sa.sa_handler = &on_tasty_stop;
    ::sigemptyset(&sa.sa_mask);
    ::sigaddset(&sa.sa_mask, SIGINT);
    ::sigaddset(&sa.sa_mask, SIGTERM);
    ::sigaddset(&sa.sa_mask, SIGQUIT);
    sa.sa_flags = SA_RESTART;
    (void)::sigaction(SIGINT, &sa, nullptr);
    (void)::sigaction(SIGTERM, &sa, nullptr);
    (void)::sigaction(SIGQUIT, &sa, nullptr);

    struct sigaction fat {};
    fat.sa_handler = &on_tasty_fatal;
    ::sigemptyset(&fat.sa_mask);
    ::sigaddset(&fat.sa_mask, SIGSEGV);
    ::sigaddset(&fat.sa_mask, SIGABRT);
    ::sigaddset(&fat.sa_mask, SIGBUS);
    fat.sa_flags = static_cast<int>(SA_ONSTACK | SA_RESETHAND);
    (void)::sigaction(SIGSEGV, &fat, nullptr);
    (void)::sigaction(SIGABRT, &fat, nullptr);
    (void)::sigaction(SIGBUS, &fat, nullptr);
}

int mister::fw::tasty_run_owner(const TastyArgs& args) {
    tasty_arm_owner_signals();
    static_assert(!tasty_boot_calls_handoff());
    TastyArgs local = args;
    if (!tasty_resolve_args(local)) {
        tasty_say("tasty: cannot resolve paths");
        return 1;
    }
    if (local.verb == TastyVerb::Play) {
        if (::access(local.movie.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            std::snprintf(buf, sizeof buf, "tasty: missing movie %s", local.movie.c_str());
            tasty_say(buf);
            return 1;
        }
        auto vfs = mister::svc::Vfs::create_at("/");
        if (vfs) {
            const int pf = tasty_prepare_play(*vfs, local);
            if (pf != 0) return pf;
        } else if (local.rom.empty()) {
            tasty_say("tasty: pass --rom");
            return 1;
        }
        if (local.rom.empty() || ::access(local.rom.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            std::snprintf(buf, sizeof buf, "tasty: missing rom %s", local.rom.c_str());
            tasty_say(buf);
            return 1;
        }
    }
    if (local.record) {
        auto rec = tasty_prepare_record(*local.record, local.movie.view());
        if (!rec) return 1;
        local.record = *rec;
    }
    auto lock = tasty_lock_owner();
    if (!lock) {
        if (lock.error().code == Errc::busy) {
            tasty_say(tasty_busy_text());
            return 2;
        }
        report("tasty lock", lock.error());
        return 2;
    }
    fw::ReturnHome home{};
    home.lock_fd = *lock;
    if (const int held = tasty_stop_and_owe()) return held;
    (void)tasty_write_pid(::getpid(), local.movie.view());

    fw::RtEvidence ev{};
    if (auto r = fw::rt_memory_init(fw::RtMode::Required, ev); !r) {
        report("mlockall", r.error());
        return 1;
    }
    fw::prefault_current_stack(hal::kFifoStackBytes, ev);

    auto compatible = hal::read_compatible(hal::kCompatiblePath);
    if (!compatible) {
        report(hal::kCompatiblePath, compatible.error());
        return 1;
    }
    auto selected = hal::select_board(*compatible);
    if (!selected) {
        report_unknown_board(*compatible);
        report("no board profile", selected.error());
        return 1;
    }
    const hal::BoardProfile& board = **selected;
    announce_board(board, *compatible);

    if (auto t = fw::rt_topology_init(board.threads, fw::RtMode::Required, ev); !t) {
        report("thread map", t.error());
        return 1;
    }
    fw::pin_current_cpu(board.main_cpu, ev.main_affinity);
    if (auto c = fw::verify_census(); !c) {
        report("census", c.error());
        return 1;
    }
    {
        const auto pinned = hal::pin_irq_affinities(board.irq_pins, board.threads, hal::kProcRoot);
        ev.irq_affinity = fw::RtSetup{pinned.all_applied, pinned.first_err};
        (void)pinned;
    }

    auto opened_windows = hal::BoardWindows::open(board, hal::FallbackNotice::Quiet);
    if (!opened_windows) {
        report("BoardWindows::open", opened_windows.error());
        return 1;
    }
    hal::BoardWindows& windows = *(new (static_cast<void*>(g_windows_storage))
                                       hal::BoardWindows(std::move(*opened_windows)));
    static xthread::RtStats stats TASTY_PERSIST(proc, tasty_rt_stats){};
    auto parts = fw::BoardParts::make(board, windows, stats);
    if (!parts) {
        report(parts.error().role, parts.error().why);
        return 1;
    }
    hal::ILinkPort& link = parts->link();
    auto exec = reactor::Executive::create(stats);
    if (!exec) {
        report("Executive::create", exec.error());
        return teardown(1, link, nullptr, IoSeat::Stopped);
    }
    const bool fabric_ready = [&link] {
        const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
        return link.ready();
    }();
    if (!fabric_ready) {
        tasty_say("tasty: FPGA not ready");
        return teardown(1, link, nullptr, IoSeat::Stopped);
    }
    auto vfs = svc::Vfs::create(svc::StorageRoot{});
    if (!vfs) {
        report("Vfs::create", vfs.error());
        return teardown(1, link, nullptr, IoSeat::Stopped);
    }
    const auto link_timing = hal::LinkTimingValues::resolve(board.timing, ERR_SITE());
    if (!link_timing) {
        report("LinkTiming", link_timing.error());
        return teardown(1, link, nullptr, IoSeat::Stopped);
    }
    const auto program = hal::ProgramGeometryValues::resolve(board.program, ERR_SITE());
    if (!program) {
        report("ProgramGeometry", program.error());
        return teardown(1, link, nullptr, IoSeat::Stopped);
    }
    const fw::Hub::BootParts boot_parts{
        .link = link,
        .bridges = parts->bridges(),
        .programmer = parts->programmer(),
        .fpga_mem = board.fpga_mem,
        .lw_window = hal::lw_region(board),
        .regions = board.regions,
        .doorbells = board.doorbells,
        .doorbell_nodes = hal::doorbell_nodes(board),
        .video = board.video,
        .kernel = board.kernel,
        .threads = board.threads,
        .exec = *exec,
        .stats = stats,
        .vfs = *vfs,
        .rt_evidence = ev,
        .kernel_rt = ev.kernel,
        .link_timing = *link_timing,
        .program = *program,
    };
    MainStopSignal stop_signal{};
    static_assert(__cpp_aligned_new >= 201606L);
    auto hub = std::make_unique<fw::Hub>(boot_parts, stop_signal);
    hub->force_vsync_adjust(args.vsync_adjust);
    hub->arm_replay_ini(args.strict);
    const hal::PhysRegion fb = board.regions[static_cast<std::size_t>(hal::RegionId::VideoFb)];
    fw::ProcessMain& process_main = hub->process_main();
    FeederReplay feeder{};
    feeder.f = &hub->replay_feeder();
    TastySession tasty{{.video = &hub->video_pump(),
                        .replay = &feeder,
                        .rec = &hub->recorder(),
                        .asks = &hub->ui_requests(),
                        .link_tx = &hub->ui_link_tx(),
                        .identity = &hub->identity_latch(),
                        .play = &hub->replay_status(),
                        .recstat = &hub->recorder_status(),
                        .encstat = &hub->encode_status(),
                        .main_stop = &process_main.stop_wake(),
                        .vfs = hub->vfs(),
                        .stop = &g_stop_requested,
                        .exit_code = &g_exit_code,
                        .video_fb = fb,
                        .direct_video_ini = &hub->owner().direct_video_ini_cell()},
                       local};
    hub->set_owner_tick(&tasty);

    app::SessionOwner& owner = hub->owner();
    owner.set_replay_save_root(fw::kTastySaveRoot);
    fw::ThreadAssembly& assembly = hub->threads();

    {
        const mister::SeatScope boot_stands_in_for_rt{mister::SeatTag::RT};
        const char* boot_argv[2] = {"tasty", ""};
        const std::span<const char* const> boot_args(boot_argv, 2);
        const auto b = hub->boot_link(boot_args);
        if (!b) {
            report("LinkSession::boot", b.error());
            return teardown(1, link, hub.get(), IoSeat::Stopped);
        }
        if (*b)
            owner.service_boot(**b);
        else
            owner.expect_boot_negotiate();
        bool boot_config_latched = false;
        for (;;) {
            process_main.run_once();
            if (!boot_config_latched && owner.boot_config_parsed()) {
                boot_config_latched = true;
                hub->latch_boot_config();
            }
            if (g_stop_requested.load(std::memory_order_seq_cst) != 0) {
                process_main.stop();
                break;
            }
            auto r = hub->pump_boot();
            if (r) break;
            if (r.error().code == Errc::cancelled) {
                report("RtMain::pump_boot", r.error());
                return teardown(1, link, hub.get(), IoSeat::Stopped);
            }
            if (r.error().code == Errc::would_block) continue;
            report("RtMain::pump_boot", r.error());
            break;
        }
        if (!boot_config_latched) hub->latch_boot_config();
        if (hub->session_state() == app::SessionState::Failed) {
            return teardown(1, link, hub.get(), IoSeat::Stopped);
        }
    }

    if (auto s = assembly.spawn(fw::RtMode::Required, hub->seat_mains()); !s) {
        report("ThreadAssembly::spawn", s.error());
        assembly.stop();
        if (auto j = assembly.join(); !j) report("ThreadAssembly::join", j.error());
        if (assembly.io_wedged()) exit_io_wedged(assembly);
        return teardown(1, link, hub.get(), IoSeat::Stopped);
    }
    if (assembly.has_io_seat()) hub->seal_boot_pump();
    Ex<void> rt_up = process_main.open();
    if (rt_up) rt_up = assembly.spawn_rt(fw::RtMode::Required, hub->rt_main());
    if (!rt_up) {
        report("spawn_rt", rt_up.error());
        assembly.mark_quiescing();
        const int rc = teardown(1, link, hub.get(), IoSeat::Running);
        assembly.stop();
        if (auto j = assembly.join(); !j) report("ThreadAssembly::join", j.error());
        if (assembly.io_wedged()) exit_io_wedged(assembly);
        return rc;
    }
    g_process_main.store(&process_main, std::memory_order_seq_cst);
    if (g_stop_requested.load(std::memory_order_seq_cst) != 0) process_main.stop();
    process_main.start();
    g_process_main.store(nullptr, std::memory_order_release);
    if (auto j = assembly.stop_and_join_rt(fw::kRtStopDeadlineNs); !j) {
        exit_wedged(assembly, j.error());
    }
    const auto ran = assembly.rt_result();

    {
        const mister::SeatScope shutdown_stands_in_for_rt{mister::SeatTag::RT};
        assembly.mark_quiescing();
        if (auto sd = hub->shutdown_link(); !sd) report("LinkSession::shutdown", sd.error());
    }
    assembly.stop();
    if (auto j = assembly.join(); !j) report("ThreadAssembly::join", j.error());
    if (assembly.io_wedged()) exit_io_wedged(assembly);
    hub.reset();
    if (!ran) report("RtMain::start", ran.error());
    const int rc = g_exit_code.load(std::memory_order_relaxed);
    if (rc != 0) return rc;
    if (!ran) return 1;
    return 0;
}
