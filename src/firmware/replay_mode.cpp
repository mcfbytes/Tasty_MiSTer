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

#include "app/session_owner.h"
#include "hal/region_id.h"
#include "hal/thread_map.h"
#include "infra/persist.h"
#include "os/process_title.h"
#include "process_standup.h"
#include "report.h"
#include "svc/vfs.h"

namespace mister::fw {

namespace {

std::atomic<int> g_exit_code TASTY_PERSIST(proc, tasty_exit_code){0};
static_assert(std::atomic<int>::is_always_lock_free);

enum class HomeLane : int { Idle = 0, Open = 1, Stopping = 2, Left = 3 };
std::atomic<int> g_home_owed TASTY_PERSIST(proc, tasty_home_owed){0};
std::atomic<int> g_deferred_stop TASTY_PERSIST(proc, tasty_deferred_stop){0};
static_assert(std::atomic<int>::is_always_lock_free);

alignas(16) unsigned char g_fatal_stack[32 * 1024] TASTY_PERSIST(proc, tasty_fatal_stack){};

class TastyVoice final : public IReportVoice {
public:
    void say(std::string_view line) const noexcept override { tasty_say(line); }
};

const TastyVoice kTastyVoice{};

hal::PhysRegion video_fb_region(std::span<const hal::PhysRegion> regions) noexcept {
    if (regions.size() != hal::kRegionCount)
        return hal::PhysRegion{};
    return regions[static_cast<std::size_t>(hal::RegionId::VideoFb)];
}

}

}

namespace {

using namespace mister;
using fw::HomeLane;

extern "C" void on_tasty_stop(int sig) {
    if (fw::ProcessStandup::latch_stop())
        return;
    const int lane = fw::g_home_owed.load(std::memory_order_seq_cst);
    if (lane == static_cast<int>(HomeLane::Open)) {
        (void)fw::tasty_launch_home();
        ::_exit(128 + sig);
    }

    if (lane == static_cast<int>(HomeLane::Stopping))
        fw::g_deferred_stop.store(sig, std::memory_order_seq_cst);
}

extern "C" void on_tasty_fatal(int sig) {
    if (fw::g_home_owed.load(std::memory_order_seq_cst) == static_cast<int>(HomeLane::Open))
        (void)fw::tasty_launch_home();
    sigset_t set;
    ::sigemptyset(&set);
    ::sigaddset(&set, sig);
    (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    ::sigemptyset(&sa.sa_mask);
    (void)::sigaction(sig, &sa, nullptr);
    (void)::raise(sig);
    ::_exit(128 + sig);
}

}

namespace mister::fw {

const std::atomic<int>& tasty_owner_stop_flag() noexcept {
    return ProcessStandup::stop_latch();
}

void tasty_owe_home() noexcept {
    g_home_owed.store(static_cast<int>(HomeLane::Open), std::memory_order_seq_cst);
}

bool tasty_home_may_launch() noexcept {
    const int lane = g_home_owed.load(std::memory_order_seq_cst);
    return lane != static_cast<int>(HomeLane::Stopping) && lane != static_cast<int>(HomeLane::Left);
}

int tasty_stop_and_owe() noexcept {
    g_deferred_stop.store(0, std::memory_order_seq_cst);
    g_home_owed.store(static_cast<int>(HomeLane::Stopping), std::memory_order_seq_cst);
    bool gone = false;
    if (const auto stopped = tasty_stop_stock())
        gone = *stopped;
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
    if (!tasty_resolve_args(args_)) {
        tasty_say("tasty: cannot resolve paths");
        return 1;
    }
    if (args_.verb == TastyVerb::Play) {
        if (::access(args_.movie.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            std::snprintf(buf, sizeof buf, "tasty: missing movie %s", args_.movie.c_str());
            tasty_say(buf);
            return 1;
        }
        auto vfs = svc::Vfs::create_at("/");
        if (vfs) {
            const int pf = tasty_prepare_play(*vfs, args_);
            if (pf != 0)
                return pf;
        } else if (args_.rom.empty()) {
            tasty_say("tasty: pass --rom");
            return 1;
        }
        if (args_.rom.empty() || ::access(args_.rom.c_str(), R_OK) != 0) {
            char buf[app::kPathMax + 32];
            std::snprintf(buf, sizeof buf, "tasty: missing rom %s", args_.rom.c_str());
            tasty_say(buf);
            return 1;
        }
    }
    if (args_.record) {
        auto rec = tasty_prepare_record(*args_.record, args_.movie.view());
        if (!rec)
            return 1;
        args_.record = *rec;
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
    home_.emplace();
    home_->lock_fd = *lock;
    if (const int held = tasty_stop_and_owe())
        return held;
    (void)tasty_write_pid(::getpid(), args_.movie.view());
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
        std::snprintf(
            buf,
            sizeof buf,
            "tasty: board %.*s",
            static_cast<int>(board.model.size()),
            board.model.data()
        );
        tasty_say(buf);
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
    (void)tasty_launch_home();
}

int ReplayMode::exit_status(const Ex<void>& ran) const noexcept {
    const int rc = g_exit_code.load(std::memory_order_relaxed);
    if (rc != 0)
        return rc;
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
    return std::make_unique<Owner>(plat, device, replay, rec, args_);
}

ReplayMode::Owner::Owner(
    const BootParts& plat,
    DeviceBlock& device,
    ReplayBlock& replay,
    RecorderBlock& rec,
    const TastyArgs& args
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
           .exit_code = g_exit_code,
           .video_fb = video_fb_region(plat.regions),
           .direct_video_ini = device.owner_.direct_video_ini_cell()},
          args
      } {

    device.video_pump_.force_vsync_adjust(args.vsync_adjust);
    device.video_pump_.arm_replay_ini(args.strict);
    device.owner_.arm_replay_ini(args.strict);
    device.owner_.set_replay_save_root(kTastySaveRoot);
    device.owner_.set_remembered_files(app::SessionOwner::RememberedFiles::Ignore);
}

bool ReplayMode::FeederReplay::take_play(const TastySession::PlayAsk& ask) noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    app::ReplayFeeder::Play p{};
    p.movie = ask.movie;
    p.rom = ask.rom;
    p.phase_us = ask.phase_us;
    p.lead = ask.lead;
    p.stop_at = ask.stop_at;
    p.ram_fill = ask.ram_fill;
    p.seeded_save = ask.seeded_save;
    p.set_settings = ask.set_settings;
    (void)f.take_play(p);
    return f.last_refusal() == app::ReplayFeeder::Refusal::None;
}

void ReplayMode::FeederReplay::take_stop() noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    (void)f.take_stop();
}

std::uint32_t ReplayMode::FeederReplay::frames() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.frames();
}

std::uint32_t ReplayMode::FeederReplay::movie_frames() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.movie_frames();
}

bool ReplayMode::FeederReplay::refused() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.last_refusal() != app::ReplayFeeder::Refusal::None;
}

bool ReplayMode::FeederReplay::active() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.stage() != app::ReplayFeeder::Stage::Idle;
}

const char* ReplayMode::FeederReplay::refusal_why() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    if (f.last_refusal() == app::ReplayFeeder::Refusal::Movie)
        return tasty_movie_refusal_token(f.last_refusal_detail());
    return app::ReplayFeeder::refusal_name(f.last_refusal());
}

std::string_view ReplayMode::FeederReplay::refusal_setting() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.last_setting();
}

bool ReplayMode::FeederReplay::refusal_setting_offered() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.last_setting_offered();
}

std::string_view ReplayMode::FeederReplay::refusal_path() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.refusal_path();
}

std::size_t ReplayMode::FeederReplay::settings_set() const noexcept {
    TASTY_SEAT_BODY(FeederReplay);
    return f.settings_set().size();
}

std::string ReplayMode::FeederReplay::setting_line(std::size_t i) const {
    TASTY_SEAT_BODY(FeederReplay);
    if (i >= f.settings_set().size())
        return {};
    const app::ReplayFeeder::SetSetting& s = f.settings_set()[i];
    return std::string(s.name) + ": using " + s.now + " for this run (your setting: " + s.was + ")";
}

}
