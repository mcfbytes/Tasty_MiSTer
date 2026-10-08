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
#include "process_main.h"
#include "report.h"
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
#include "svc/config_snapshot.h"
#include "svc/deadzone_rule.h"
#include "svc/vfs.h"
#include "app/replay_feeder.h"
#include "cores/movie_codec.h"
#include "tasty_ctl.h"
#include "tasty_session.h"

namespace mister::fw {

DiagSampler::Sources TastyHub::diag_sources_(const BootParts& plat) noexcept {
    const DiagSampler::DeviceCells d = device_.diag_sources(plat);
    return DiagSampler::Sources{
        .mgl_row0 = d.mgl_row0,
        .window_counts = d.window_counts,
        .video_stats = device_.video_pump_.stats_cell(),
        .video_geometry = device_.video_pump_.geometry_cell(),
        .video_wire = device_.video_pump_.wire(),
        .diag = d.diag,
        .pause_expiries = d.pause_expiries,
        .recover_polls = d.recover_polls,
        .save_write_failures = d.save_write_failures,
        .fallbacks = d.fallbacks,
        .doorbell = d.doorbell,
        .round_timing = d.round_timing,
        .frames = d.frames,
        .replay = replay_.replay_status_,
        .rec_capture = rec_.rec_status_,
        .rec_encode = rec_.encode_status_,
        .rec_write = rec_.rec_write_status_,
        .rec_avi = rec_.avi_write_status_,
        .screenshots = d.screenshots,
    };
}

TastyHub::TastyHub(const BootParts& plat, app::IStopSignal& stop)
    : device_{plat, stop, DeviceBlock::Front{}},
      tasty_sink_{device_.video_pump_, &device_.session_.identity()},
      shot_pump_{app::ScreenshotPump::Wiring{.queue = device_.shots_}},
      replay_{plat, device_, shot_pump_, null_osd_}, rec_{plat, device_, replay_.replay_status_},
      router_{app::CmdRouter::Wiring{.video = device_.video_pump_,
                                     .shots = shot_pump_,
                                     .replay = replay_.replay_feeder_,
                                     .rec = rec_.recorder_,
                                     .ui_requests = device_.owner_.ui_requests(),
                                     .stats = plat.stats,
                                     .link_tx = device_.ui_inbox_}},
      fifo_(DeviceBlock::open_cmd_fifo(router_)),
      rt_main_{plat.exec, plat.link_timing,
               app::RtMain::Wiring{
                   .session = infra::OptRef<app::LinkSession>{device_.session_},
                   .input = infra::OptRef<app::InputEmit>{device_.input_emit_},
                   .osd = infra::OptRef<app::OsdWire>{device_.osd_wire_},
                   .timer = infra::OptRef<reactor::RoundTimer>{plat.round_timer},
                   .wire = infra::OptRef<app::InputWire>{device_.input_wire_},
                   .frames = infra::OptRef<app::CoreFrameCounter>{device_.frame_counter_},
                   .replay = infra::OptRef<app::ReplayGate>{replay_.replay_gate_},
                   .replay_ring = infra::OptRef<app::ReplayRing>{replay_.replay_ring_},
               }},
      assembly_{plat.threads,
                plat.stats,
                device_.events_,
                plat.rt_evidence,
                device_.main_wake_,
                ThreadAssembly::DiagWires{.log = device_.diag_log_,
                                          .rt_lane = plat.rt_lane,
                                          .transitioning = device_.transitioning_},
                UiMain::Wires{.wake = device_.ui_wake_, .uart_handoffs = device_.uart_handoffs_},
                UiMain::Wiring{.fifo = fifo_ ? &*fifo_ : nullptr,
                               .mgl = nullptr,
                               .video = device_.video_pump_,
                               .ui_sink = tasty_sink_,
                               .owner_events = device_.owner_events_,
                               .shots = shot_pump_,
                               .replay = replay_.replay_feeder_,
                               .recorder = rec_.recorder_,
                               .link_rx = device_.ui_rx_},
                diag_sources_(plat)},
      process_main_{device_.owner_, assembly_, device_.main_wake_} {
    device_.open_wires(plat);
    device_.open_seats(plat, DeviceBlock::Seats{.mains = seat_mains_, .diag = assembly_.diag()});
    rec_.open_seats(seat_mains_);
    device_.grant_pauses(seat_mains_, assembly_.ui());
}

TastyHub::~TastyHub() { assembly_.stop_join_or_exit(); }

void TastyHub::latch_boot_config() noexcept {
    auto c = std::make_unique<svc::ConfigSnapshot>();
    if (device_.config_cell_.sample_into(*c) == 0) c = std::make_unique<svc::ConfigSnapshot>();
    device_.input_build_.set_cfg_deadzone_rules(svc::parse_deadzone_rules(*c));
}

}  // namespace mister::fw

using mister::fw::report;
extern char** environ;

namespace {

using namespace mister;

class TastyVoice final : public fw::IReportVoice {
public:
    void say(std::string_view line) const noexcept override { fw::tasty_say(line); }
};
const TastyVoice kTastyVoice{};

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

int teardown(int rc, hal::ILinkPort& link, fw::TastyHub* hub, IoSeat io) {
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
                  assembly.tid(SeatTag::Io));
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
    set_report_voice(&kTastyVoice);
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

    fw::warn_vm_compaction(ev.vm_compaction, "tasty", stderr);
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
    if (local.record)
        tasty_warn_record_writeback(local.record->view(),
                                    hal::seat_of(board.threads, SeatTag::RT).cpu);

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

    static reactor::RoundTimer round_timer TASTY_PERSIST(proc, tasty_round_timer){};
    static xthread::LogLane rt_lane TASTY_PERSIST(proc, tasty_rt_lane){};
    auto parts = fw::BoardParts::make(board, windows, stats);
    if (!parts) {
        report(parts.error().role, parts.error().why);
        return 1;
    }
    hal::ILinkPort& link = parts->link();
    auto exec = reactor::Executive::create(
        stats, {.round_timer = infra::OptRef<reactor::RoundTimer>{round_timer},
                .log_lane = infra::OptRef<xthread::LogLane>{rt_lane}});
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
    const fw::BootParts boot_parts{
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
        .round_timer = round_timer,
        .rt_lane = rt_lane,
        .vfs = *vfs,
        .rt_evidence = ev,
        .kernel_rt = ev.kernel,
        .link_timing = *link_timing,
        .program = *program,
    };
    MainStopSignal stop_signal{};
    static_assert(__cpp_aligned_new >= 201606L);
    auto hub = std::make_unique<fw::TastyHub>(boot_parts, stop_signal);
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
    owner.set_remembered_files(app::SessionOwner::RememberedFiles::Ignore);
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
