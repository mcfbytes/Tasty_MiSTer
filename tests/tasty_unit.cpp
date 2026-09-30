// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/check.h"

#include "app/event.h"
#include "app/identity_latch.h"
#include "app/link_tx_channel.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "app/ui_request_ring.h"
#include "app/video_pump.h"
#include "cores/md5.h"
#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "cores/registry.h"
#include "cores/rom_digest.h"
#include "hal/boards_table.h"
#include "hal/core_signals.h"
#include "hal/spi_transport.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/link_op.h"
#include "svc/video_service.h"
#include "svc/vfs.h"
#include "tasty_cli.h"
#include "tasty_ctl.h"
#include "tasty_hub.h"
#include "tasty_osd.h"
#include "tasty_registry.h"
#include "tasty_session.h"
#include "tasty_sink.h"
#include "tasty_splash.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef TASTY_HAVE_MINIZIP
#define TASTY_HAVE_MINIZIP 0
#endif
#if TASTY_HAVE_MINIZIP
#include <minizip/zip.h>
#endif

using namespace mister;

namespace {

struct FakeReplay final : fw::TastySession::Replay {
    TASTY_SEAT_RESIDENT(Ui);
    bool play_ok = true;
    bool refuse_later = false;
    bool feeding = true;
    const char* why = "none";
    unsigned plays = 0;
    unsigned stops = 0;
    fw::TastySession::PlayAsk last{};
    std::vector<std::string> lines{};
    bool take_play(const fw::TastySession::PlayAsk& ask) noexcept override {
        TASTY_SEAT_BODY(FakeReplay);
        ++plays;
        last = ask;
        if (!play_ok) {
            why = "movie_io";
            return false;
        }
        return true;
    }
    void take_stop() noexcept override {
        TASTY_SEAT_BODY(FakeReplay);
        ++stops;
    }
    std::uint32_t frames() const noexcept override { return 12; }
    std::uint32_t movie_frames() const noexcept override { return 12; }
    bool refused() const noexcept override { return refuse_later; }
    bool active() const noexcept override { return feeding; }
    const char* refusal_why() const noexcept override { return why; }
    std::string_view setting{};
    std::string_view path{};
    bool offered = false;
    std::string_view refusal_setting() const noexcept override { return setting; }
    bool refusal_setting_offered() const noexcept override { return offered; }
    std::string_view refusal_path() const noexcept override { return path; }
    std::size_t settings_set() const noexcept override { return lines.size(); }
    std::string setting_line(std::size_t i) const override { return lines[i]; }
};

struct Tmp {
    std::string root;
    explicit Tmp(const char* tag) {
        root = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + tag;
        root += "XXXXXX";
        if (::mkdtemp(root.data()) == nullptr) root.clear();
    }
    ~Tmp() {
        if (root.empty()) return;
        (void)::unlink((root + "/lock").c_str());
        (void)::unlink((root + "/pid").c_str());
        (void)::unlink((root + "/status").c_str());
        (void)::unlink((root + "/status.tmp").c_str());
        (void)::unlink((root + "/m.fm2").c_str());
        (void)::unlink((root + "/r.nes").c_str());
        (void)::unlink((root + "/snes.bk2").c_str());
        (void)::unlink((root + "/gen.bk2").c_str());
        (void)::unlink((root + "/psx.bk2").c_str());
        (void)::rmdir(root.c_str());
    }
    std::string p(const char* n) const { return root + "/" + n; }
    bool put(const char* n, std::string_view b) const {
        std::FILE* f = std::fopen(p(n).c_str(), "wb");
        if (f == nullptr) return false;
        const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
        std::fclose(f);
        return ok;
    }
};

void publish_id(app::IdentityLatch& ident, std::string_view rbf) {
    const SeatScope rt{SeatTag::RT};
    ident.publish(rbf, rbf, svc::JoyPlan{}, "", false, false, nullptr, nullptr, nullptr, {});
}

void publish_end(app::ReplayStatusCell& cell, app::ReplayEnd end) {
    const SeatScope rt{SeatTag::RT};
    app::ReplayStatus s{};
    s.end = end;
    s.movie_frame = 3;
    cell.publish(s);
}

fw::TastyArgs play_args() {
    fw::TastyArgs a{};
    a.verb = fw::TastyVerb::Play;
    a.no_splash = true;
    a.return_after_s = 0;
    (void)a.movie.assign("m.fm2");
    (void)a.rom.assign("r.nes");
    (void)a.core.assign("/media/fat/_Console/NES.rbf");
    return a;
}

struct Sess {
    FakeReplay replay{};
    app::IdentityLatch ident{};
    app::ReplayStatusCell play{};
    app::RecorderStatusCell recstat{};
    app::UiRequestRing asks{};
    app::LinkTxChannel tx{};
    std::atomic<int> stop{0};
    std::atomic<int> want_stock{0};
    std::atomic<int> exit_code{0};
    fw::TastyArgs args = play_args();
    fw::TastySession::Wiring w{};
    std::optional<fw::TastySession> s{};
    const SeatScope ui{SeatTag::Ui};

    explicit Sess(const svc::Vfs* vfs = nullptr) {
        w.replay = &replay;
        w.identity = &ident;
        w.play = &play;
        w.recstat = &recstat;
        w.asks = &asks;
        w.link_tx = &tx;
        w.stop = &stop;
        w.want_stock = &want_stock;
        w.exit_code = &exit_code;
        w.vfs = vfs;
        s.emplace(w, args);
    }
    void tick() { s->tick(); }

    std::optional<proto::LinkOp> wire_op() {
        const SeatScope rt{SeatTag::RT};
        return tx.pop();
    }

    bool load_asked() {
        const SeatScope main{SeatTag::Unbound};
        const auto a = asks.pop();
        return a && infra::as<app::UiRequest::LoadCore>(*a).has_value();
    }
};

struct VresLink final : hal::ISpiTransport, hal::ICoreSignals {
    std::uint32_t vtime = 0;
    bool ready() const { return true; }
    Ex<hal::CoreIdentity> identify() override { return hal::CoreIdentity{}; }
    hal::CoreCapabilities capabilities() const override { return {}; }
    void set_core_reset(bool) override {}
    void select(hal::ChipSelect) override {
        open_ = true;
        first_ = true;
    }
    void deselect() override { open_ = false; }
    Ex<hal::SpiWord> transfer(hal::SpiWord w) override {
        if (!open_) return hal::SpiWord{0};
        if (first_) {
            first_ = false;
            op_ = static_cast<std::uint8_t>(w.v & 0xFFu);
            cursor_ = 0;
            return hal::SpiWord{0};
        }
        const std::uint16_t words[9] = {0x0001,
                                        0x0500,
                                        0x0000,
                                        0x02d0,
                                        0x0000,
                                        0x2148,
                                        0x0000,
                                        static_cast<std::uint16_t>(vtime & 0xFFFFu),
                                        static_cast<std::uint16_t>(vtime >> 16)};
        std::uint16_t in = 0;
        if (op_ == svc::kUioGetVres && cursor_ < 9) in = words[cursor_];
        ++cursor_;
        return hal::SpiWord{in};
    }
    Ex<void> block_write(std::span<const std::uint8_t>) override { return {}; }
    Ex<void> block_read(std::span<std::uint8_t>) override { return {}; }

private:
    bool open_ = false, first_ = true;
    std::uint8_t op_ = 0;
    unsigned cursor_ = 0;
};

struct ZeroClock final : os::IClock {
    std::chrono::nanoseconds now() const override { return std::chrono::nanoseconds{0}; }
};

struct PumpRig {
    Tmp root{"/tasty_tasty_pump"};
    std::optional<svc::Vfs> vfs{};
    ZeroClock clock{};
    std::optional<app::VideoPump> pump{};
    VresLink link{};
    std::int64_t t = 0;

    PumpRig() {
        if (auto v = svc::Vfs::create_at(root.root)) vfs.emplace(std::move(*v));
        if (vfs) pump.emplace(*vfs, clock, hal::board_by_id(hal::BoardId::De10Nano).video);
    }

    void core(std::uint32_t seq, std::uint32_t vtime) {
        const SeatScope rt{SeatTag::RT};
        link.vtime = vtime;
        pump->wire().reset_geometry(seq);
        for (int i = 0; i < 3; ++i) {
            pump->wire().on_rt_round(link, t);
            t += 600'000'000;
        }
    }
    app::VideoGeometryRecord tick() {
        const SeatScope ui{SeatTag::Ui};
        pump->tick();
        return pump->geometry_record();
    }
};

void to_run(Sess& x) {
    x.tick();
    publish_id(x.ident, "NES");
    x.tick();
    x.tick();
}

void home(Sess& x) {
    x.tick();
    publish_id(x.ident, "MENU");
    x.tick();
    x.tick();
}

}  // namespace

void test_cli_play() {
    const char* const av[] = {"tasty", "play", "/m/a.fm2", "--rom", "/r/a.nes", "--stay"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a) return;
    CHECK(a->verb == fw::TastyVerb::Play);
    CHECK(a->movie.view() == "/m/a.fm2");
    CHECK(a->rom.view() == "/r/a.nes");
    CHECK(a->stay);
    CHECK(a->return_after_s == 30);
    CHECK(a->vsync_adjust == 0);
}

void test_cli_vsync() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--vsync-adjust", "1"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->vsync_adjust == 1);
    const char* const bad[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--vsync-adjust", "2"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad)).has_value());
}

void test_cli_rec() {
    const char* const av[] = {"tasty", "rec", "start", "--record", "/media/fat/cap"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->verb == fw::TastyVerb::RecStart);
    CHECK(a->record.view() == "/media/fat/cap");
}

void test_cli_hashes() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--hashes"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->hashes && !a->hashes_only);
    const char* const only[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--hashes-only"};
    const auto b = fw::parse_tasty_args(std::span<const char* const>(only));
    CHECK(b.has_value() && b->hashes_only);
}

void test_boot_no_handoff() { CHECK(!fw::tasty_boot_calls_handoff()); }

void test_cli_play_needs_rom() {
    const char* const av[] = {"tasty", "play", "m.fm2"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(av)).has_value());
}

void test_registry() {
    CHECK(fw::tasty_plays("NES"));
    CHECK(fw::tasty_plays("SNES"));
    CHECK(fw::tasty_plays("MegaDrive"));
    CHECK(fw::tasty_plays("PSX"));
    CHECK(fw::tasty_plays("MENU"));
    CHECK(!fw::tasty_plays("AO486"));
    CHECK(!fw::tasty_plays("N64"));
    CHECK(fw::tasty_core_table().size() == 4);
    for (const auto& f : fw::tasty_core_table()) {
        CHECK(f.profile != nullptr);
        CHECK(f.kind == f.profile->kind);
        CHECK(f.name == f.profile->name);
    }
}

void test_codec_path() {
    CHECK(cores::movie_codec_for_path("a.fm2") != nullptr);
    CHECK(cores::movie_codec_for_path("a.lsmv") != nullptr);
    CHECK(cores::movie_codec_for_path("a.gmv") != nullptr);
    CHECK(cores::movie_codec_for_path("a.bk2") == nullptr);
}

void test_null_osd() {
    const SeatScope ui{SeatTag::Ui};
    fw::NullOsdClose c;
    c.close_osd();
}

void test_movie_system_path() {
    const auto nes = cores::movie_system_for_path("a.fm2");
    CHECK(nes.has_value());
    if (nes) {
        CHECK(nes->kind == cores::CoreKind::Generic);
        CHECK(nes->conf_str_name == "NES");
        CHECK(nes->codec != nullptr);
        CHECK(cores::movie_system_supported(*nes));
    }
    CHECK(cores::movie_system_for_path("a.lsmv").has_value());
    CHECK(cores::movie_system_for_path("a.gmv").has_value());
    CHECK(!cores::movie_system_for_path("a.bk2").has_value());
    CHECK(cores::movie_system_plays("NES"));
    CHECK(cores::movie_system_plays("PSX"));
    CHECK(!cores::movie_system_plays("MENU"));
    CHECK(!cores::movie_system_plays("N64"));
}

void test_splash_pixel() {
    CHECK(fw::TastySplash::pixel(0, 0) == 0xFF101018u);
    CHECK(fw::TastySplash::pixel(16, 16) != 0xFF101018u);
}

void test_replay_end_names() {
    CHECK(std::string_view(app::replay_end_name(app::ReplayEnd::Refused)).size() > 0);
    CHECK(std::string_view(app::replay_end_name(app::ReplayEnd::CoreSwitch)).size() > 0);
}

void test_lock_paths() {
    CHECK(std::string_view(fw::kTastyLockPath) == "/tmp/tasty.lock");
    CHECK(std::string_view(fw::kMenuRbfName) == "menu.rbf");
}

void test_return_home() {
    int n = 0;
    {
        fw::ReturnHome h{};
        h.spawned = &n;
    }
    CHECK(n == 1);
    n = 0;
    {
        fw::ReturnHome h{};
        h.spawned = &n;
        h.armed = false;
    }
    CHECK(n == 0);
}

void test_return_home_error_path() {
    int n = 0;
    auto fail = [&] {
        fw::ReturnHome h{};
        h.spawned = &n;
        return 1;
    };
    CHECK(fail() == 1);
    CHECK(n == 1);
}

void test_lock_unlink() {
    Tmp t("/tasty_tasty_lock_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    auto fd = fw::tasty_lock_owner();
    CHECK(fd.has_value());
    if (!fd) return;
    CHECK(fw::tasty_write_pid(4242).has_value());
    CHECK(fw::tasty_write_status("x\n").has_value());
    CHECK(fw::tasty_read_pid() == 4242);
    fw::tasty_unlock(*fd);
    CHECK(!fw::tasty_read_pid());
    auto fd2 = fw::tasty_lock_owner();
    CHECK(fd2.has_value());
    if (fd2) fw::tasty_unlock(*fd2);
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_prepare_record_stem() {
    Tmp t("/tasty_tasty_rec_");
    CHECK(!t.root.empty());
    app::PathText rec{};
    CHECK(rec.assign(t.root));
    const auto out = fw::tasty_prepare_record(rec, "/media/fat/tas/klmz3-smb.fm2");
    CHECK(out.has_value());
    if (!out) return;
    CHECK(out->view().find("klmz3-smb") != std::string_view::npos);
    CHECK(out->view().find(t.root) != std::string_view::npos);
}

void test_resolve_relative() {
    Tmp t("/tasty_tasty_cwd_");
    CHECK(!t.root.empty());
    CHECK(t.put("m.fm2", "x"));
    char old[4096]{};
    CHECK(::getcwd(old, sizeof old) != nullptr);
    CHECK(::chdir(t.root.c_str()) == 0);
    fw::TastyArgs a{};
    CHECK(a.movie.assign("m.fm2"));
    CHECK(fw::tasty_resolve_args(a));
    CHECK(::chdir(old) == 0);
    CHECK(a.movie.view().find(t.root) != std::string_view::npos);
    CHECK(a.movie.view().find("m.fm2") != std::string_view::npos);
}

void test_info_missing() {
    Tmp t("/tasty_tasty_info_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs) return;
    CHECK(fw::tasty_info_movie(*vfs, "missing.fm2") == 1);
}

void test_check_missing() {
    Tmp t("/tasty_tasty_ckm_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs) return;
    CHECK(fw::tasty_check_movie(*vfs, "missing.fm2", "r.nes") == 1);
}

void test_busy_text() {
    Tmp t("/tasty_tasty_busy_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    CHECK(fw::tasty_write_pid(99, "/media/fat/tas/smb.fm2").has_value());
    const std::string msg = fw::tasty_busy_text();
    CHECK(msg.find("smb.fm2") != std::string::npos);
    CHECK(msg.find("99") != std::string::npos);
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_stop_idle() {
    Tmp t("/tasty_tasty_idle_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    CHECK(fw::tasty_client_act(fw::TastyVerb::Stop) == fw::TastyClientAct::Idle);
    CHECK(fw::tasty_client_act(fw::TastyVerb::RecStop) == fw::TastyClientAct::Idle);
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_stop_owner_signaled() {
    Tmp t("/tasty_tasty_sig_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    char comm[32]{};
    const int fd = ::open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    if (fd >= 0) {
        const ssize_t n = ::read(fd, comm, sizeof comm - 1);
        ::close(fd);
        if (n > 0 && comm[n - 1] == '\n') comm[n - 1] = '\0';
    }
    static char owner_comm[32];
    std::snprintf(owner_comm, sizeof owner_comm, "%s", comm);
    fw::tasty_set_owner_comm(owner_comm);
    CHECK(fw::tasty_write_pid(::getpid()).has_value());
    CHECK(fw::tasty_client_act(fw::TastyVerb::Stop) == fw::TastyClientAct::Signaled);
    CHECK(fw::tasty_client_act(fw::TastyVerb::RecStop) == fw::TastyClientAct::WroteFifo);
    (void)::unlink(pid.c_str());
    fw::tasty_set_owner_comm("tasty");
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_rec_start_checks_the_owner() {
    Tmp t("/tasty_tasty_rec_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    fw::tasty_set_owner_comm("tasty");
    CHECK(fw::tasty_write_pid(::getpid()).has_value());
    CHECK(!fw::tasty_rec_start_joins_owner());
    static char comm[32]{};
    const int fd = ::open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        const ssize_t n = ::read(fd, comm, sizeof comm - 1);
        ::close(fd);
        if (n > 0 && comm[n - 1] == '\n') comm[n - 1] = '\0';
    }
    fw::tasty_set_owner_comm(comm);
    CHECK(fw::tasty_rec_start_joins_owner());
    (void)::unlink(pid.c_str());
    CHECK(!fw::tasty_rec_start_joins_owner());
    fw::tasty_set_owner_comm("tasty");
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_stop_stale_pid() {
    Tmp t("/tasty_tasty_stale_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    fw::tasty_set_owner_comm("tasty");
    CHECK(fw::tasty_write_pid(1).has_value());
    CHECK(fw::tasty_client_act(fw::TastyVerb::Stop) == fw::TastyClientAct::Idle);
    (void)::unlink(pid.c_str());
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_load_edge_rereads_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump) return;
    app::IdentityLatch ident{};
    publish_id(ident, "NES");
    fw::TastySink sink{*r.pump, &ident};
    r.core(1, 1'671'559);
    auto g = r.tick();
    CHECK(g.valid);
    CHECK_EQ(g.vtime, 1'671'559u);
    CHECK_EQ(g.core_seq, 1u);
    {
        const SeatScope ui{SeatTag::Ui};
        r.pump->on_pause();
    }
    r.core(3, 1'663'934);
    g = r.tick();
    CHECK_EQ(g.vtime, 1'671'559u);
    {
        const SeatScope ui{SeatTag::Ui};
        sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    }
    for (int i = 0; i < 3; ++i)
        g = r.tick();
    CHECK(g.valid);
    CHECK_EQ(g.vtime, 1'663'934u);
    CHECK_EQ(g.core_seq, 3u);
    CHECK_EQ(r.pump->stats().edges, 1u);
}

void test_start_waits_for_video_edge() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump) return;
    fw::TastySink sink{*r.pump, nullptr};
    Sess x;
    x.w.video = &*r.pump;
    x.s.emplace(x.w, x.args);
    x.tick();
    publish_id(x.ident, "NES");
    for (int i = 0; i < 3; ++i)
        x.tick();
    CHECK_EQ(x.replay.plays, 0u);
    sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    x.tick();
    x.tick();
    CHECK_EQ(x.replay.plays, 1u);
}

void test_take_play_refusal() {
    Sess x;
    x.replay.play_ok = false;
    to_run(x);
    CHECK(x.replay.plays == 1);
    CHECK(x.exit_code.load() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_later_refusal_ends_session() {
    Sess x;
    to_run(x);
    CHECK(x.replay.plays == 1);
    CHECK(x.exit_code.load() == 0);
    x.replay.refuse_later = true;
    x.replay.why = "setting";
    x.tick();
    CHECK(x.exit_code.load() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.want_stock.load() == 1);
}

void test_epoch_ambiguous_then_finished() {
    Sess x;
    to_run(x);
    CHECK(x.replay.plays == 1);
    publish_end(x.play, app::ReplayEnd::EpochAmbiguous);
    x.tick();
    CHECK(x.replay.stops == 0);
    CHECK(x.want_stock.load() == 0);
    CHECK(x.exit_code.load() == 0);
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.want_stock.load() == 1);
    CHECK(x.exit_code.load() == 0);
}

void test_epoch_ambiguous_final_leaves() {
    Sess x;
    to_run(x);
    publish_end(x.play, app::ReplayEnd::EpochAmbiguous);
    x.tick();
    CHECK(x.exit_code.load() == 0);
    x.replay.feeding = false;
    x.tick();
    CHECK(x.exit_code.load() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.want_stock.load() == 1);
}

void test_replay_ends_leave() {
    const app::ReplayEnd ends[] = {app::ReplayEnd::Finished,   app::ReplayEnd::Stopped,
                                   app::ReplayEnd::Superseded, app::ReplayEnd::CoreSwitch,
                                   app::ReplayEnd::RefLost,    app::ReplayEnd::NoReference,
                                   app::ReplayEnd::Refused};
    for (const auto end : ends) {
        Sess x;
        to_run(x);
        CHECK(x.replay.plays == 1);
        publish_end(x.play, end);
        x.tick();
        if (end == app::ReplayEnd::Finished) x.tick();
        home(x);
        CHECK(x.replay.stops >= 1);
        CHECK(x.want_stock.load() == 1);
        if (end != app::ReplayEnd::Finished && end != app::ReplayEnd::Stopped) {
            CHECK(x.exit_code.load() == 1);
        }
    }
}

struct StderrTo {
    Tmp t{"/tasty_tasty_err_"};
    int saved = -1;
    StderrTo() {
        std::fflush(stderr);
        saved = ::dup(2);
        const int fd = ::open(t.p("status").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd >= 0) {
            (void)::dup2(fd, 2);
            ::close(fd);
        }
    }
    std::string text() {
        std::fflush(stderr);
        if (saved >= 0) {
            (void)::dup2(saved, 2);
            ::close(saved);
            saved = -1;
        }
        std::string out;
        if (std::FILE* f = std::fopen(t.p("status").c_str(), "rb")) {
            char buf[512];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                out.append(buf, n);
            std::fclose(f);
        }
        return out;
    }
    ~StderrTo() { (void)text(); }
};

void test_stay_finished_holds() {
    Sess x;
    x.args.stay = true;
    x.s.emplace(x.w, x.args);
    to_run(x);
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    CHECK(x.replay.stops == 0);
    CHECK(x.want_stock.load() == 0);
}

void test_stay_stop_leaves() {
    Sess x;
    x.args.stay = true;
    x.s.emplace(x.w, x.args);
    to_run(x);
    x.stop.store(1);
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.want_stock.load() == 1);
}

void test_rec_only_stop_leaves() {
    Sess x;
    x.args.verb = fw::TastyVerb::RecStart;
    x.args.no_splash = true;
    x.s.emplace(x.w, x.args);
    x.tick();
    CHECK(x.replay.plays == 0);
    x.stop.store(1);
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.want_stock.load() == 1);
}

void test_stale_generation() {
    Tmp t("/tasty_tasty_gen_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs) return;
    Sess x{&*vfs};
    x.args = play_args();
    (void)x.args.movie.assign("m.fm2");
    x.s.emplace(x.w, x.args);
    publish_id(x.ident, "NES");
    x.tick();
    x.tick();
    x.tick();
    CHECK(x.replay.plays == 0);
    publish_id(x.ident, "NES");
    x.tick();
    x.tick();
    CHECK(x.replay.plays == 1);
}

void test_status_force() {
    Tmp t("/tasty_tasty_st_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    Sess x;
    to_run(x);
    x.tick();
    const auto first = fw::tasty_read_status();
    CHECK(first.has_value());
    publish_end(x.play, app::ReplayEnd::Finished);
    struct stat a {};
    struct stat b {};
    CHECK(::stat(st.c_str(), &a) == 0);
    x.tick();
    CHECK(::stat(st.c_str(), &b) == 0);
    CHECK(a.st_mtime == b.st_mtime && a.st_size == b.st_size);
    x.tick();
    home(x);
    auto last = fw::tasty_read_status();
    CHECK(last.has_value());
    if (last) CHECK(last->find("finished") != std::string::npos);
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_status_frames_total() {
    Tmp t("/tasty_tasty_fr_");
    CHECK(!t.root.empty());
    const std::string lock = t.p("lock");
    const std::string pid = t.p("pid");
    const std::string st = t.p("status");
    fw::tasty_set_ctl_paths(lock.c_str(), pid.c_str(), st.c_str());
    Sess x;
    to_run(x);
    x.tick();
    const auto s = fw::tasty_read_status();
    CHECK(s.has_value());
    if (s) CHECK(s->find("\"frames\":12") != std::string::npos);
    home(x);
    fw::tasty_set_ctl_paths(fw::kTastyLockPath, fw::kTastyPidPath, fw::kTastyStatusPath);
}

void test_check_exits() {
    Tmp t("/tasty_tasty_ck_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs) return;
    std::string gmv(64, '\0');
    std::memcpy(gmv.data(), "Gens Movie TEST", 15);
    gmv[0x0F] = 'A';
    gmv[0x14] = '6';
    gmv[0x15] = '6';
    CHECK(t.put("none.gmv", gmv));
    CHECK(fw::tasty_check_movie(*vfs, "none.gmv", "r.nes") == 0);
    std::vector<std::uint8_t> rom(16 + 32 * 1024);
    for (std::size_t i = 0; i < rom.size(); ++i)
        rom[i] = static_cast<std::uint8_t>(i * 7u + 3u);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 2;
    rom[5] = 0;
    rom[6] = 0;
    rom[7] = 0;
    CHECK(t.put("r.nes", std::string_view(reinterpret_cast<const char*>(rom.data()), rom.size())));
    cores::mra::Md5 h;
    h.update(std::span<const std::uint8_t>(rom.data() + 16, 32 * 1024));
    const auto d = h.digest();
    static constexpr char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64 = "romChecksum base64:";
    for (std::size_t i = 0; i < 16; i += 3) {
        const std::uint32_t n = (std::uint32_t{d[i]} << 16) |
                                (i + 1 < 16 ? std::uint32_t{d[i + 1]} << 8 : 0u) |
                                (i + 2 < 16 ? std::uint32_t{d[i + 2]} : 0u);
        b64 += k[(n >> 18) & 63];
        b64 += k[(n >> 12) & 63];
        b64 += i + 1 < 16 ? k[(n >> 6) & 63] : '=';
        b64 += i + 2 < 16 ? k[n & 63] : '=';
    }
    const std::string good = "version 3\n" + b64 +
                             "\nromFilename x\nguid 1\nport0 1\nport1 0\nport2 0\n"
                             "fourscore 0\nFDS 0\n|0|........|\n";
    CHECK(t.put("good.fm2", good));
    CHECK(fw::tasty_check_movie(*vfs, "good.fm2", "r.nes") == 0);
    rom[20] ^= 0xFF;
    CHECK(
        t.put("bad.nes", std::string_view(reinterpret_cast<const char*>(rom.data()), rom.size())));
    CHECK(fw::tasty_check_movie(*vfs, "good.fm2", "bad.nes") == 3);
    (void)::unlink(t.p("none.gmv").c_str());
    (void)::unlink(t.p("good.fm2").c_str());
    (void)::unlink(t.p("bad.nes").c_str());
}

#if TASTY_HAVE_MINIZIP
bool write_bk2(const std::string& path, std::string_view platform) {
    zipFile zf = ::zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr) return false;
    const std::string header = std::string("MovieVersion BizHawk v2.0.0\nPlatform ") +
                               std::string(platform) + "\nSHA1 ABC\n";
    const char* names[] = {"Header.txt", "Input Log.txt"};
    const std::string bodies[] = {header, "[Input]\nLogKey:#Reset|\n|..|\n[/Input]\n"};
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        if (::zipOpenNewFileInZip(zf, names[i], nullptr, nullptr, 0, nullptr, 0, nullptr, 0, 0) !=
            ZIP_OK) {
            ok = false;
            break;
        }
        if (::zipWriteInFileInZip(zf, bodies[i].data(), static_cast<unsigned>(bodies[i].size())) !=
            ZIP_OK)
            ok = false;
        if (::zipCloseFileInZip(zf) != ZIP_OK) ok = false;
    }
    if (::zipClose(zf, nullptr) != ZIP_OK) ok = false;
    return ok;
}

void test_bk2_sniff() {
    Tmp t("/tasty_tasty_bk2_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs) return;
    CHECK(write_bk2(t.p("snes.bk2"), "SNES"));
    CHECK(write_bk2(t.p("gen.bk2"), "GEN"));
    CHECK(write_bk2(t.p("psx.bk2"), "PSX"));
    const auto snes = cores::movie_system_for(*vfs, "snes.bk2");
    const auto gen = cores::movie_system_for(*vfs, "gen.bk2");
    const auto psx = cores::movie_system_for(*vfs, "psx.bk2");
    CHECK(snes && snes->conf_str_name == "SNES");
    CHECK(gen && gen->conf_str_name == "MegaDrive");
    CHECK(psx && psx->conf_str_name == "PSX");
}
#endif

void test_cli_run_flags() {
    const char* const av[] = {"tasty",     "play", "m.fm2",    "--rom", "r.nes",
                              "--stop-at", "3600", "--linger", "0",     "--strict"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a) return;
    CHECK(a->stop_at == std::optional<std::uint32_t>{3600});
    CHECK_EQ(a->return_after_s, 0u);
    CHECK(a->strict);
    const char* const dflt[] = {"tasty", "play", "m.fm2", "--rom", "r.nes"};
    const auto d = fw::parse_tasty_args(std::span<const char* const>(dflt));
    CHECK(d.has_value() && !d->stop_at && !d->strict && d->return_after_s == 30);
    const char* const old[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--return-after", "5"};
    const auto o = fw::parse_tasty_args(std::span<const char* const>(old));
    CHECK(o.has_value() && o->return_after_s == 5);
    const char* const zero[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--stop-at", "0"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(zero)).has_value());
    const char* const help[] = {"tasty", "--help"};
    const auto h = fw::parse_tasty_args(std::span<const char* const>(help));
    CHECK(h.has_value() && h->verb == fw::TastyVerb::Help);
    const char* const help2[] = {"tasty", "play", "m.fm2", "--help"};
    const auto h2 = fw::parse_tasty_args(std::span<const char* const>(help2));
    CHECK(h2.has_value() && h2->verb == fw::TastyVerb::Help);
}

void test_session_play_ask() {
    for (const bool strict : {false, true}) {
        Sess x;
        x.args.stop_at = 3600;
        x.args.strict = strict;
        x.s.emplace(x.w, x.args);
        to_run(x);
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(x.replay.last.stop_at == std::optional<std::uint32_t>{3600});
        CHECK(x.replay.last.set_settings == !strict);
    }
}

void test_session_prints_settings_once() {
    Sess x;
    to_run(x);
    std::string err;
    {
        StderrTo cap;
        x.replay.lines.push_back("System Type: using NTSC for this run (your setting: PAL)");
        x.tick();
        x.replay.lines.push_back("RAM Clear: using $00 for this run (your setting: No)");
        x.tick();
        x.tick();
        err = cap.text();
    }
    CHECK(err == "tasty: System Type: using NTSC for this run (your setting: PAL)\n"
                 "tasty: RAM Clear: using $00 for this run (your setting: No)\n");
    CHECK(x.exit_code.load() == 0);
}

void test_refusal_sentences() {
    struct Case {
        const char* why;
        std::string_view setting;
        std::string_view path;
        bool strict;
        bool offered;
        const char* want;
    };
    const Case cases[] = {
        {"setting",
         "RAM Clear",
         {},
         true,
         true,
         "tasty: replay refused (--strict): the core's RAM Clear is not the movie's; without "
         "--strict tasty sets it for this run\n"},
        {"setting",
         "Region",
         {},
         true,
         false,
         "tasty: replay refused: this movie needs a Region this core does not offer\n"},
        {"setting",
         "Region",
         {},
         false,
         false,
         "tasty: replay refused: this movie needs a Region this core does not offer\n"},
        {"setting",
         "RAM Clear",
         {},
         false,
         true,
         "tasty: replay refused: tasty could not set the core's RAM Clear for this run\n"},
        {"firmware",
         {},
         "/media/fat/games/PSX/cd_bios.rom",
         false,
         false,
         "tasty: replay refused: the movie was recorded with another BIOS; it needs its own at "
         "/media/fat/games/PSX/cd_bios.rom\n"},
        {"savestate",
         {},
         {},
         false,
         false,
         "tasty: replay refused: m.fm2 starts from a savestate or saved game, not from "
         "power-on\n"},
    };
    for (const Case& c : cases) {
        Sess x;
        x.args.strict = c.strict;
        x.s.emplace(x.w, x.args);
        to_run(x);
        std::string err;
        {
            StderrTo cap;
            x.replay.refuse_later = true;
            x.replay.why = c.why;
            x.replay.setting = c.setting;
            x.replay.offered = c.offered;
            x.replay.path = c.path;
            x.tick();
            err = cap.text();
        }
        CHECK(err == c.want);
        CHECK(err.find("menu") == std::string::npos);
        CHECK(x.exit_code.load() == 1);
    }
}

void test_session_hides_the_osd_first() {
    Sess x;
    x.tick();
    const auto op = x.wire_op();
    CHECK(op.has_value());
    const auto vis = op ? infra::as<proto::LinkOp::SetOsdVisible>(*op) : std::nullopt;
    CHECK(vis.has_value() && vis->show == proto::LinkOp::OsdShow::Off);
    CHECK(x.load_asked());
}

void test_session_waits_for_the_osd_hide() {
    Sess x;
    unsigned filled = 0;
    while (x.tx.push(proto::LinkOp::SettleCoreOptions{}))
        ++filled;
    CHECK(filled > 0u);
    x.tick();
    CHECK(!x.load_asked());
    (void)x.wire_op();
    x.tick();
    CHECK(x.load_asked());
    bool hidden = false;
    while (const auto op = x.wire_op()) {
        const auto vis = infra::as<proto::LinkOp::SetOsdVisible>(*op);
        if (vis && vis->show == proto::LinkOp::OsdShow::Off) hidden = true;
    }
    CHECK(hidden);
}

void publish_running(app::ReplayStatusCell& cell, std::int32_t frame) {
    const SeatScope rt{SeatTag::RT};
    app::ReplayStatus s{};
    s.level = app::ReplayLevel::Running;
    s.movie_frame = frame;
    cell.publish(s);
}

void test_recorded_replay_rereads_the_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump) return;
    fw::TastySink sink{*r.pump, nullptr};
    Sess x;
    x.w.video = &*r.pump;
    (void)x.args.record.assign("/tmp/tasty_tasty_rec/cap");
    x.s.emplace(x.w, x.args);
    r.core(1, 19'108);
    x.tick();
    publish_id(x.ident, "MegaDrive");
    sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    for (int i = 0; i < 3; ++i) {
        (void)r.tick();
        x.tick();
    }
    CHECK_EQ(x.replay.plays, 1u);
    const auto passes = [&r](int n) {
        const SeatScope rt{SeatTag::RT};
        for (int i = 0; i < n; ++i) {
            r.pump->wire().on_rt_round(r.link, r.t);
            r.t += 600'000'000;
        }
    };
    passes(2);
    CHECK_EQ(r.tick().vtime, 19'108u);
    r.link.vtime = 1'668'814;
    passes(2);
    CHECK_EQ(r.tick().vtime, 19'108u);
    publish_running(x.play, 59);
    x.tick();
    (void)r.tick();
    passes(2);
    CHECK_EQ(r.tick().vtime, 19'108u);
    publish_running(x.play, 60);
    x.tick();
    (void)r.tick();
    passes(2);
    CHECK_EQ(r.tick().vtime, 1'668'814u);
}

void test_unrecorded_replay_leaves_the_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump) return;
    fw::TastySink sink{*r.pump, nullptr};
    Sess x;
    x.w.video = &*r.pump;
    x.s.emplace(x.w, x.args);
    r.core(1, 19'108);
    x.tick();
    publish_id(x.ident, "MegaDrive");
    sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    for (int i = 0; i < 3; ++i) {
        (void)r.tick();
        x.tick();
    }
    const auto passes = [&r](int n) {
        const SeatScope rt{SeatTag::RT};
        for (int i = 0; i < n; ++i) {
            r.pump->wire().on_rt_round(r.link, r.t);
            r.t += 600'000'000;
        }
    };
    passes(2);
    (void)r.tick();
    r.link.vtime = 1'668'814;
    publish_running(x.play, 600);
    x.tick();
    (void)r.tick();
    passes(2);
    CHECK_EQ(r.tick().vtime, 19'108u);
}

int main() {
    test_cli_play();
    test_cli_vsync();
    test_cli_rec();
    test_cli_hashes();
    test_boot_no_handoff();
    test_cli_play_needs_rom();
    test_registry();
    test_codec_path();
    test_null_osd();
    test_movie_system_path();
    test_splash_pixel();
    test_replay_end_names();
    test_lock_paths();
    test_return_home();
    test_return_home_error_path();
    test_lock_unlink();
    test_prepare_record_stem();
    test_resolve_relative();
    test_info_missing();
    test_check_missing();
    test_busy_text();
    test_stop_idle();
    test_stop_owner_signaled();
    test_stop_stale_pid();
    test_rec_start_checks_the_owner();
    test_load_edge_rereads_geometry();
    test_start_waits_for_video_edge();
    test_take_play_refusal();
    test_later_refusal_ends_session();
    test_epoch_ambiguous_then_finished();
    test_epoch_ambiguous_final_leaves();
    test_replay_ends_leave();
    test_stay_finished_holds();
    test_stay_stop_leaves();
    test_rec_only_stop_leaves();
    test_stale_generation();
    test_status_force();
    test_status_frames_total();
    test_check_exits();
    test_cli_run_flags();
    test_session_play_ask();
    test_session_prints_settings_once();
    test_session_hides_the_osd_first();
    test_refusal_sentences();
    test_session_waits_for_the_osd_hide();
    test_recorded_replay_rereads_the_geometry();
    test_unrecorded_replay_leaves_the_geometry();
#if TASTY_HAVE_MINIZIP
    test_bk2_sniff();
#endif
    if (failures) {
        std::printf("tasty_unit: %d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("tasty_unit: all checks passed\n");
    return 0;
}
