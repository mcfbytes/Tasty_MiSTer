// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_mode.h"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "app/session_owner.h"
#include "hal/axi.h"
#include "hal/region_id.h"
#include "hal/thread_map.h"
#include "infra/persist.h"
#include "os/child_process.h"
#include "os/process_id.h"
#include "os/process_title.h"
#include "process_standup.h"
#include "report.h"
#include "svc/vfs.h"
#include "tasty_owner_lock.h"
#include "tasty_pid_file.h"
#include "tasty_preflight.h"
#include "tasty_record.h"
#include "tasty_say.h"
#include "tasty_writeback.h"

namespace mister::fw {

namespace {

inline constexpr std::size_t kFatalStackBytes = 32 * 1024;
#if !defined(__GLIBC__)
static_assert(
    kFatalStackBytes >= SIGSTKSZ,
    "the fatal handler's alternate stack must hold one signal frame"
);
#endif
alignas(16) unsigned char g_fatal_stack[kFatalStackBytes] TASTY_PERSIST(proc, tasty_fatal_stack){};

class TastyVoice final : public IReportVoice {
public:
    void say(std::string_view line) const noexcept override { tasty_say(line); }
};

const TastyVoice kTastyVoice{};

}

}

namespace {

using namespace mister;

extern "C" void on_tasty_stop(int sig) {
    if (fw::ProcessStandup::latch_stop())
        return;
    const fw::RunExit status = fw::after_signal(os::ChildProcessExit::SignalNumber{sig});
    const fw::HomeLane::Lane lane = fw::HomeLane::lane();
    if (lane == fw::HomeLane::Lane::Open)
        fw::HomeLane::launch_and_exit(status);

    if (lane == fw::HomeLane::Lane::Stopping)
        fw::HomeLane::defer_stop(status);
}

extern "C" void on_tasty_fatal(int sig) {
    if (fw::HomeLane::lane() == fw::HomeLane::Lane::Open)
        fw::HomeLane::launch();
    sigset_t set;
    ::sigemptyset(&set);
    ::sigaddset(&set, sig);
    (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    ::sigemptyset(&sa.sa_mask);
    (void)::sigaction(sig, &sa, nullptr);
    (void)::raise(sig);
    ::_exit(fw::process_status(fw::after_signal(os::ChildProcessExit::SignalNumber{sig})));
}

}

namespace mister::fw {

const std::atomic<int>& tasty_owner_stop_flag() noexcept {
    return ProcessStandup::stop_latch();
}

void tasty_arm_owner_signals() noexcept {
    stack_t ss{};
    ss.ss_sp = g_fatal_stack;
    ss.ss_size = sizeof g_fatal_stack;
    ss.ss_flags = 0;
    (void)::sigaltstack(&ss, nullptr);

    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    ::sigemptyset(&ign.sa_mask);
    (void)::sigaction(SIGHUP, &ign, nullptr);
    (void)::sigaction(SIGPIPE, &ign, nullptr);

    struct sigaction sa{};
    sa.sa_handler = &on_tasty_stop;
    ::sigemptyset(&sa.sa_mask);
    ::sigaddset(&sa.sa_mask, SIGINT);
    ::sigaddset(&sa.sa_mask, SIGTERM);
    ::sigaddset(&sa.sa_mask, SIGQUIT);
    sa.sa_flags = SA_RESTART;
    (void)::sigaction(SIGINT, &sa, nullptr);
    (void)::sigaction(SIGTERM, &sa, nullptr);
    (void)::sigaction(SIGQUIT, &sa, nullptr);

    struct sigaction fat{};
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

void ReplayMode::title_process() noexcept {
    set_report_voice(&kTastyVoice);
    if (!area_)
        return;
    if (auto t = os::ProcessTitle::create(area_->argc, area_->argv, area_->env, "tasty"); !t)
        report("ProcessTitle::create", t.error());
}

ReplayMode::Exit ReplayMode::prepare() {
    tasty_arm_owner_signals();
    auto resolved = tasty_resolve_args(args_);
    if (!resolved) {
        tasty_say("tasty: cannot resolve paths");
        return as_exit(mister::fw::refused());
    }
    args_ = std::move(*resolved);
    if (run_.replays()) {
        if (::access(args_.movie.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            tasty_say(
                tasty_render(buf, "tasty: missing movie {}", std::string_view{args_.movie.c_str()})
            );
            return as_exit(mister::fw::refused());
        }
        auto vfs = svc::Vfs::create_at("/");
        if (vfs) {
            auto rom = TastyPreflight{*vfs}.play_rom(args_);
            if (!rom)
                return as_exit(mister::fw::refused());
            args_.rom = *rom;
        } else if (args_.rom.empty()) {
            tasty_say("tasty: pass --rom");
            return as_exit(mister::fw::refused());
        }
        if (args_.rom.empty() || ::access(args_.rom.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            tasty_say(
                tasty_render(buf, "tasty: missing rom {}", std::string_view{args_.rom.c_str()})
            );
            return as_exit(mister::fw::refused());
        }
    }
    if (args_.record) {
        auto rec = tasty_prepare_record(*args_.record, args_.movie.view());
        if (!rec)
            return as_exit(mister::fw::refused());
        args_.record = *rec;
    }
    auto lock = TastyOwnerLock::take(rt_.paths);
    if (!lock) {
        if (lock.error().code == Errc::busy) {
            tasty_say(TastyPidFile{rt_.paths}.busy_text());
            return as_exit(busy_owner());
        }
        report("tasty lock", lock.error());
        return as_exit(busy_owner());
    }
    home_.emplace();
    const TastyOwnerLock& held = home_->adopt(std::move(*lock));
    if (auto stopped = HomeLane::stop_and_owe(stock_); !stopped)
        return as_exit(mister::fw::refused());

    if (auto wrote = held.write_pid(os::ProcessId::self(), args_.movie.view()); !wrote) {
        report("tasty pid", wrote.error());
        return as_exit(mister::fw::refused());
    }
    return {};
}

int ReplayMode::refused(const Error& e, Refusal r) {
    report(r.what, e);
    return 1;
}

void ReplayMode::QuietVoice::board(
    const hal::BoardProfile& board,
    const hal::CompatibleBlob& blob
) const {
    for (const std::string_view s : board.compatible) {
        if (!blob.contains(s))
            continue;
        char buf[128];
        tasty_say(tasty_render(buf, "tasty: board {}", board.model));
        break;
    }
    if (args_.record)
        tasty_warn_record_writeback(
            args_.record->view(),
            hal::seat_of(board.threads, SeatTag::RT).cpu
        );
}

void ReplayMode::QuietVoice::unknown_board(const hal::CompatibleBlob&) const {
    tasty_say("tasty: no board profile claims this device tree");
}

void ReplayMode::QuietVoice::fabric_not_ready(const hal::BoardProfile&) const {
    tasty_say("tasty: FPGA not ready");
}

void ReplayMode::QuietVoice::wedged(std::string_view line) const noexcept {
    tasty_say(line);
    HomeLane::launch();
}

int ReplayMode::exit_status(const Ex<void>& ran) const noexcept {

    const RunExit recorded = run_exit_.sample().value;
    if (failed(recorded))
        return process_status(recorded);
    return ran ? 0 : 1;
}

std::unique_ptr<SessionMode::Front> ReplayMode::front(const BootParts&) {
    return std::make_unique<NoFront>();
}

std::unique_ptr<SessionMode::Face> ReplayMode::NoFront::face(
    const BootParts&,
    DeviceBlock& device
) {
    return std::make_unique<Sink>(device);
}

std::unique_ptr<SessionMode::Tail> ReplayMode::tail(
    const BootParts& plat,
    DeviceBlock& device,
    ReplayBlock& replay,
    RecorderBlock& rec
) {
    return std::make_unique<Owner>(plat, device, replay, rec, run_exit_, args_, run_, status_);
}

ReplayMode::Owner::Owner(
    const BootParts& plat,
    DeviceBlock& device,
    ReplayBlock& replay,
    RecorderBlock& rec,
    RunExitCell& run_exit,
    const TastyArgs& args,
    const TastySession::Run& run,
    TastyStatusFile& status
)
    : feeder_{replay.replay_feeder_},
      session_{
          {.video = infra::OptRef{device.video_pump_},
           .replay = feeder_,
           .rec = rec.recorder_,
           .asks = device.owner_.ui_requests(),
           .link_tx = device.ui_inbox_,
           .identity = device.session_.identity(),
           .play = replay.replay_status_,
           .recstat = rec.rec_status_,
           .encstat = rec.encode_status_,
           .main_stop = device.main_stop_,
           .vfs = plat.vfs,
           .stop = ProcessStandup::stop_latch(),
           .run_exit = run_exit,
           .status = status,
           .video_fb = hal::declared_region(plat.regions, hal::RegionId::VideoFb)
                           .value_or(hal::PhysRegion{}),
           .direct_video_ini = device.owner_.direct_video_ini_cell()},
          args,
          run
      } {

    device.video_pump_.force_vsync_adjust(args.vsync_adjust);
    device.video_pump_.arm_replay_ini(args.strict);
    device.owner_.arm_replay_ini(args.strict);
    device.owner_.set_replay_save_root(kTastySaveRoot);
    device.owner_.set_remembered_files(app::SessionOwner::RememberedFiles::Ignore);
}

bool ReplayMode::FeederReplay::take_play(const app::ReplayFeeder::Play& play) noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    (void)f.take_play(play);
    return f.last_refusal() == app::ReplayFeeder::Refusal::None;
}

void ReplayMode::FeederReplay::take_stop() noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    (void)f.take_stop();
}

app::FrameCount ReplayMode::FeederReplay::movie_frames() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.movie_frames();
}

bool ReplayMode::FeederReplay::active() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.stage() != app::ReplayFeeder::Stage::Idle;
}

std::optional<ReplayRefusal> ReplayMode::FeederReplay::refusal() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    const app::ReplayFeeder::Refusal why = f.last_refusal();
    if (why == app::ReplayFeeder::Refusal::None)
        return std::nullopt;
    ReplayRefusal r{
        .why = why,
        .setting = f.last_setting(),
        .setting_offered = f.last_setting_offered(),
        .path = f.refusal_path()
    };
    if (why == app::ReplayFeeder::Refusal::Movie) {

        r.movie = cores::movie_refusal_of(
            Error{Errc::bad_format, infra::kNoSite, f.last_refusal_detail()}
        );
    }
    return r;
}

IReplayPort::SettingNo ReplayMode::FeederReplay::settings_set() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return SettingNo{f.settings_set().size()};
}

std::string ReplayMode::FeederReplay::setting_line(IReplayPort::SettingNo i) const {
    TASTY_SEAT_BODY(FeederReplay);
    const app::ReplayFeeder::SetSetting* s = i.row_in(f.settings_set());
    if (s == nullptr)
        return {};
    return std::string(s->name)
        + ": using "
        + s->now
        + " for this run (your setting: "
        + s->was
        + ")";
}

}
