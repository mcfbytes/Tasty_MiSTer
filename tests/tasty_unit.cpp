// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/check.h"
#include "support/prelude_stand.h"
#include "support/video_quiet.h"

#include "app/encode_status.h"
#include "app/step_count.h"
#include "app/event.h"
#include "app/identity_latch.h"
#include "app/link_tx_channel.h"
#include "app/bitstream_resolve.h"
#include "app/rec_control.h"
#include "app/recorder_control.h"
#include "app/recorder_status.h"
#include "infra/diag_log.h"
#include "app/replay_feeder.h"
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
#include "proto/link.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/child_process.h"
#include "os/clock.h"
#include "proto/link_op.h"
#include "svc/video_service.h"
#include "svc/vfs.h"
#include "tasty_cli.h"
#include "tasty_cli_refusal.h"
#include "tasty_client.h"
#include "tasty_movie.h"
#include "tasty_owner_lock.h"
#include "tasty_paths.h"
#include "tasty_pid_file.h"
#include "tasty_preflight.h"
#include "tasty_record.h"
#include "tasty_runtime.h"
#include "tasty_say.h"
#include "tasty_sentences.h"
#include "replay_mode.h"
#include "tasty_home_lane.h"
#include "tasty_return_home.h"
#include "tasty_osd.h"
#include "tasty_registry.h"
#include "tasty_session.h"
#include "tasty_sink.h"
#include "tasty_splash.h"
#include "tasty_status_file.h"
#include "tasty_stock_control.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ctime>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <thread>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <minizip/zip.h>
#include "support/stand_ini.h"

using namespace mister;

namespace {

struct FakeReplay final : fw::IReplayPort {
    TASTY_SEAT_RESIDENT(Ui);
    bool play_ok = true;
    bool feeding = true;
    std::optional<fw::ReplayRefusal> ref{};
    unsigned plays = 0;
    unsigned stops = 0;
    app::ReplayFeeder::Play last{};
    std::vector<std::string> lines{};

    bool take_play(const app::ReplayFeeder::Play& play) noexcept override {
        TASTY_SEAT_BODY(FakeReplay);
        ++plays;
        last = play;
        if (!play_ok) {
            ref = fw::ReplayRefusal{.why = app::PlayRefusal::Why::MovieIo};
            return false;
        }
        return true;
    }

    void take_stop() noexcept override {
        TASTY_SEAT_BODY(FakeReplay);
        ++stops;
    }

    app::FrameCount movie_frames() const noexcept override { return app::FrameCount{12}; }

    bool active() const noexcept override { return feeding; }

    std::optional<fw::ReplayRefusal> refusal() const noexcept override { return ref; }

    SettingNo settings_set() const noexcept override { return SettingNo{lines.size()}; }

    std::string setting_line(SettingNo i) const override { return lines[i.v]; }
};

struct Tmp {
    std::string root;

    explicit Tmp(const char* tag) {
        root = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + tag;
        root += "XXXXXX";
        if (::mkdtemp(root.data()) == nullptr)
            root.clear();
    }

    ~Tmp() {
        if (root.empty())
            return;
        (void)::unlink((root + "/lock").c_str());
        (void)::unlink((root + "/pid").c_str());
        (void)::unlink((root + "/status").c_str());
        (void)::unlink((root + "/status.tmp").c_str());
        (void)::unlink((root + "/m.fm2").c_str());
        (void)::unlink((root + "/r.nes").c_str());
        (void)::unlink((root + "/snes.bk2").c_str());
        (void)::unlink((root + "/gen.bk2").c_str());
        (void)::unlink((root + "/psx.bk2").c_str());
        (void)::unlink((root + "/nes.bk2").c_str());
        (void)::unlink((root + "/n64.bk2").c_str());
        (void)::rmdir(root.c_str());
    }

    std::string p(const char* n) const { return root + "/" + n; }

    bool put(const char* n, std::string_view b) const {
        std::FILE* f = std::fopen(p(n).c_str(), "wb");
        if (f == nullptr)
            return false;
        const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
        std::fclose(f);
        return ok;
    }
};

fw::TastyRuntime runtime_in(std::string_view dir) {
    auto paths = fw::TastyPaths::in(dir);
    CHECK(paths.has_value());
    fw::TastyRuntime rt{};
    if (paths)
        rt.paths = *paths;
    return rt;
}

void publish_id(app::IdentityLatch& ident, std::string_view rbf) {
    const SeatScope rt{SeatTag::RT};
    app::SessionIdentity published{};
    (void)published.core.assign(rbf);
    (void)published.rbf.assign(rbf);
    published.stem = app::RememberedStem::of(rbf, {});
    ident.publish(published);
}

void publish_end(app::ReplayStatusCell& cell, app::ReplayEnd end) {
    const SeatScope rt{SeatTag::RT};
    app::ReplayStatus s{};
    s.end = end;
    s.movie_frame = app::MovieFrame{3};
    cell.publish(s);
}

fw::TastyArgs play_args() {
    fw::TastyArgs a{};
    a.verb = fw::TastyVerb::Play;
    a.no_splash = true;
    a.linger = std::chrono::seconds{0};
    (void)a.movie.assign("m.fm2");
    (void)a.rom.assign("r.nes");
    (void)a.core.assign("/media/fat/_Console/NES.rbf");
    return a;
}

struct Sess {

    struct Binds {
        app::RecorderControl* rec = nullptr;
        const app::EncodeStatusCell* encstat = nullptr;
        const xthread::Telemetry<std::uint8_t, SeatTag::Main>* direct_video_ini = nullptr;
    };

    static svc::Vfs open_tmp() {
        auto v = svc::Vfs::create_at("/tmp");
        if (!v)
            std::abort();
        return std::move(*v);
    }

    svc::Vfs own_vfs = open_tmp();
    FakeReplay replay{};
    app::IdentityLatch ident{};
    app::ReplayStatusCell play{};
    app::RecorderStatusCell recstat{};
    app::EncodeStatusCell encstat{};
    app::UiRequestRing asks{};
    app::LinkTxChannel<mister::SeatTag::Ui> tx{};
    app::RecControlChannel rec_control{xthread::Polled{}, xthread::Polled{}};
    app::RecControlChannel::Worker capture{rec_control.worker()};
    app::RecWriteStatusCell rec_write{};

    app::RecorderControl rec{app::RecorderControl::Wiring{
        .control = infra::OptRef<app::RecControlChannel>{rec_control},
        .status = infra::OptRef<const app::RecorderStatusCell>{recstat},
        .writer = infra::OptRef<const app::RecWriteStatusCell>{rec_write},
        .root = "/"
    }};
    xthread::WakeFlag main_stop{};
    xthread::Telemetry<std::uint8_t, SeatTag::Main> dv_ini{};
    std::atomic<int> stop{0};
    fw::RunExitCell run_exit{};
    fw::TastyArgs args = play_args();
    fw::TastySession::PlayRun play_run{};
    fw::TastySession::RecordOnlyRun rec_run{};
    fw::TastyRuntime runtime{};
    fw::TastyStatusFile status{runtime.paths};
    fw::TastySession::Wiring w;
    std::optional<fw::TastySession> s{};
    const SeatScope ui{SeatTag::Ui};

    explicit Sess(const svc::Vfs* vfs = nullptr, const std::atomic<int>* stop_flag = nullptr)
        : Sess(vfs, stop_flag, Binds{}, fw::TastyRuntime{}) {}

    Sess(
        const svc::Vfs* vfs,
        const std::atomic<int>* stop_flag,
        Binds b,
        const fw::TastyRuntime& rt = {}
    )
        : runtime(rt),
          status(runtime.paths),
          w{.replay = replay,
            .rec = b.rec != nullptr ? *b.rec : rec,
            .asks = asks,
            .link_tx = tx,
            .identity = ident,
            .play = play,
            .recstat = recstat,
            .encstat = b.encstat != nullptr ? *b.encstat : encstat,
            .main_stop = main_stop,
            .vfs = vfs != nullptr ? *vfs : own_vfs,
            .stop = stop_flag != nullptr ? *stop_flag : stop,
            .run_exit = run_exit,
            .status = status,
            .direct_video_ini = b.direct_video_ini != nullptr ? *b.direct_video_ini : dv_ini} {

        {
            const SeatScope as_main{SeatTag::Main};
            dv_ini.publish(std::uint8_t{0});
        }
        s.emplace(w, args, play_run);
    }

    void tick() {
        w.rec.tick();
        s->tick();
    }

    void capture_level(
        app::RecState state,
        app::RecVerdict verdict,
        app::RecVerdict end = app::RecVerdict::None
    ) {
        {
            const SeatScope cap{SeatTag::Capture};
            if (verdict != app::RecVerdict::None) {
                for (;;) {
                    auto job = capture.take();
                    if (!job)
                        break;
                    job->verdict = verdict;
                    job.complete();
                }
            }
            app::RecorderStatus r{};
            r.gen = app::ControlGeneration{1};
            r.state = state;
            r.end = end;
            recstat.publish(r);
        }
        rec.tick();
    }

    [[nodiscard]] int exit_code() const { return fw::process_status(run_exit.sample().value); }

    std::optional<proto::LinkOp> wire_op() {
        const SeatScope rt{SeatTag::RT};
        return tx.pop();
    }

    bool load_asked() {
        const SeatScope as_main{SeatTag::Main};
        const auto a = asks.pop();
        return a && infra::as<app::UiRequest::LoadCore>(*a).has_value();
    }
};

struct VresLink final : hal::ISpiTransport, hal::ICoreSignals {

    proto::Link view{*this};
    std::uint32_t vtime = 0;

    bool ready() const { return true; }

    Ex<hal::CoreIdentity> identify() override { return hal::CoreIdentity{}; }

    hal::CoreCapabilities latch_capabilities() override { return {}; }

    void set_core_reset(bool) override {}

    void select(hal::ChipSelect) override {
        open_ = true;
        first_ = true;
    }

    void deselect() override { open_ = false; }

    Ex<hal::SpiWord> transfer(hal::SpiWord w) override {
        if (!open_)
            return hal::SpiWord{0};
        if (first_) {
            first_ = false;
            op_ = static_cast<std::uint8_t>(w.v & 0xFFu);
            cursor_ = 0;
            return hal::SpiWord{0};
        }
        const std::uint16_t words[9] = {
            0x0001,
            0x0500,
            0x0000,
            0x02d0,
            0x0000,
            0x2148,
            0x0000,
            static_cast<std::uint16_t>(vtime & 0xFFFFu),
            static_cast<std::uint16_t>(vtime >> 16)
        };
        std::uint16_t in = 0;
        if (op_ == proto::byte(proto::UioOp::GetVres) && cursor_ < 9)
            in = words[cursor_];
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
    mister::os::RawMonotonicClock::time_point now() const override {
        return mister::os::RawMonotonicClock::time_point{std::chrono::nanoseconds{0}};
    }
};

struct PumpRig {
    Tmp root{"/tasty_tasty_pump"};
    std::optional<svc::Vfs> vfs{};
    ZeroClock clock{};
    mister::testing::QuietVideoEnv quiet{};
    app::LinkTxChannel<mister::SeatTag::Ui> tx{};
    mister::testing::FakeLinkRouter replies{};
    std::optional<app::VideoPump> pump{};
    VresLink link{};
    std::int64_t t = 0;

    PumpRig() {
        if (auto v = svc::Vfs::create_at(root.root))
            vfs.emplace(std::move(*v));
        if (vfs)
            pump.emplace(
                *vfs,
                clock,
                hal::board_by_id(hal::BoardId::De10Nano).video,
                infra::OptRef<svc::adv7513::II2cAdapter>{},
                quiet.hdmi,
                mister::testing::video_wiring(quiet.activity, quiet.gates, tx, replies)
            );

        if (pump) {
            const SeatScope ui{SeatTag::Ui};
            pump->wire().publish_prelude_level(app::PreludeLevel{.core = {}, .settled = true});
        }
    }

    void core(std::uint32_t seq, std::uint32_t vtime) {
        const auto now = os::RawMonotonicClock::time_point{std::chrono::nanoseconds{t}};
        const proto::BindGeneration gen{static_cast<std::uint16_t>(seq)};
        {
            const SeatScope rt{SeatTag::RT};
            link.vtime = vtime;
            pump->wire().bind_core(app::CoreEdgeSeq{seq}, gen, now);
            pump->wire().reset_geometry(seq);
        }
        {
            const SeatScope ui{SeatTag::Ui};
            pump->wire().publish_prelude_level(app::PreludeLevel{.core = gen, .settled = true});
        }
        const SeatScope rt{SeatTag::RT};
        for (int i = 0; i < 3; ++i) {
            pump->wire().on_rt_round(link.view, t);
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

}

void test_cli_play() {
    const char* const av[] = {"tasty", "play", "/m/a.fm2", "--rom", "/r/a.nes", "--stay"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a)
        return;
    CHECK(a->verb == fw::TastyVerb::Play);
    CHECK(a->movie.view() == "/m/a.fm2");
    CHECK(a->rom.view() == "/r/a.nes");
    CHECK(a->stay);
    CHECK(a->linger == std::chrono::seconds{30});
    CHECK(a->vsync_adjust == app::VsyncAdjust::Off);
}

void test_cli_vsync() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--vsync-adjust", "1"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->vsync_adjust == app::VsyncAdjust::Match);
    const char* const bad[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--vsync-adjust", "2"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad)).has_value());
}

void test_cli_rec() {
    const char* const av[] = {"tasty", "rec", "start", "--record", "/media/fat/cap"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->verb == fw::TastyVerb::RecStart);
    CHECK(a->record && a->record->view() == "/media/fat/cap");
    CHECK(a->rec.codec == app::RecCodec::Cscd && a->rec.every.count<std::uint16_t>() == 1);
    CHECK(a->rec.motion == app::RecMotion::Auto);
    CHECK(a->rec.from_frame == app::MovieFrame{-1} && a->rec.to_frame == app::MovieFrame{-1});
}

void test_cli_record_options() {
    const char* const av[] = {"tasty", "play",      "m.fm2", "--rom",    "r.nes", "--record",
                              "/x",    "--codec",   "ZMBV",  "--motion", "full",  "--scale",
                              "half",  "--every",   "4",     "--from",   "10",    "--to",
                              "20",    "--segment", "32M"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a)
        return;
    CHECK(a->rec.codec == app::RecCodec::Zmbv);
    CHECK(a->rec.motion == app::RecMotion::Full);
    CHECK(a->rec.scale == app::RecScale::Half);
    CHECK(a->rec.every.count<std::uint16_t>() == 4);
    CHECK(a->rec.from_frame == app::MovieFrame{10} && a->rec.to_frame == app::MovieFrame{20});
    CHECK(a->rec.segment_bytes == proto::FileSize{32ull << 20});
    const char* const bad_codec[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--codec", "ffv1"};
    const auto bc = fw::parse_tasty_args(std::span<const char* const>(bad_codec));
    CHECK(!bc.has_value());
    if (!bc)
        CHECK(
            std::string_view(fw::TastyRefusal::sentence_for(bc.error()))
            == "--codec is cscd or zmbv"
        );

    const char* const no_record[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--codec", "zmbv"};
    const auto nr = fw::parse_tasty_args(std::span<const char* const>(no_record));
    CHECK(!nr.has_value());
    if (!nr)
        CHECK(
            std::string_view(fw::TastyRefusal::sentence_for(nr.error()))
            == "--codec, --scale, --motion, --every, --from, --to and --segment need --record"
        );

    const auto needs_record = [](const auto& argv) {
        const auto d = fw::parse_tasty_args(std::span<const char* const>(argv));
        CHECK(!d.has_value());
        if (!d)
            CHECK(
                std::string_view(fw::TastyRefusal::sentence_for(d.error()))
                    .ends_with("need --record")
            );
    };
    const char* const dflt_codec[] =
        {"tasty", "play", "m.fm2", "--rom", "r.nes", "--codec", "cscd"};
    const char* const dflt_scale[] =
        {"tasty", "play", "m.fm2", "--rom", "r.nes", "--scale", "auto"};
    const char* const dflt_every[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--every", "1"};
    const char* const dflt_motion[] =
        {"tasty", "play", "m.fm2", "--rom", "r.nes", "--motion", "auto"};
    needs_record(dflt_codec);
    needs_record(dflt_scale);
    needs_record(dflt_every);
    needs_record(dflt_motion);
    const char* const bad_motion[] =
        {"tasty", "play", "m.fm2", "--rom", "r.nes", "--record", "/x", "--motion", "nope"};
    const auto bm = fw::parse_tasty_args(std::span<const char* const>(bad_motion));
    CHECK(!bm.has_value());
    if (!bm)
        CHECK(
            std::string_view(fw::TastyRefusal::sentence_for(bm.error()))
            == "--motion is auto, off, small or full"
        );
    const char* const bad_slice[] =
        {"tasty", "play", "m.fm2", "--rom", "r.nes", "--from", "8", "--to", "8"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad_slice)).has_value());
    const char* const bad_every[] = {"tasty", "rec", "start", "--record", "/x", "--every", "0"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad_every)).has_value());
    const char* const bad_seg[] = {"tasty", "rec", "start", "--record", "/x", "--segment", "0"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad_seg)).has_value());
    const char* const bad_from[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--from", "-1"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad_from)).has_value());
    for (const char* bound : {"--from", "--to"}) {
        const char* const rec_slice[] = {"tasty", "rec", "start", "--record", "/x", bound, "10"};
        const auto rs = fw::parse_tasty_args(std::span<const char* const>(rec_slice));
        CHECK(!rs.has_value());
        if (!rs)
            CHECK(
                std::string_view(fw::TastyRefusal::sentence_for(rs.error()))
                == "--from and --to count movie frames; use them with tasty play --record"
            );
    }
    const char* const bad_ram[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--ram-init", "7"};
    const auto br = fw::parse_tasty_args(std::span<const char* const>(bad_ram));
    CHECK(!br.has_value());
    if (!br)
        CHECK(
            std::string_view(fw::TastyRefusal::sentence_for(br.error()))
            == "--ram-init is zero, ff or random"
        );
}

void test_session_passes_record_options() {
    Tmp t("/tasty_tasty_recopt_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    const std::string media = t.p("media");
    const std::string fat = media + "/fat";
    CHECK(::mkdir(media.c_str(), 0755) == 0);
    CHECK(::mkdir(fat.c_str(), 0755) == 0);
    app::RecControlChannel control{xthread::Polled{}, xthread::Polled{}};
    auto capture = control.worker();
    app::RecorderControl rec(
        app::RecorderControl::Wiring{
            .control = infra::OptRef<app::RecControlChannel>{control},
            .root = media
        }
    );
    Sess x{nullptr, nullptr, {.rec = &rec}};
    x.args.rec.codec = app::RecCodec::Zmbv;
    x.args.rec.motion = app::RecMotion::Full;
    x.args.rec.every = *app::RecEvery::of(3);
    x.args.rec.from_frame = app::MovieFrame{4};
    x.args.rec.to_frame = app::MovieFrame{9};
    x.args.record.emplace();
    (void)x.args.record->assign(fat + "/cap");
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    app::RecRequest got{};
    {
        const SeatScope cap{SeatTag::Capture};
        auto job = capture.take();
        CHECK(job);
        if (job)
            got = *job;
    }
    CHECK(got.op == app::RecOp::Arm);
    CHECK(got.mode == app::RecMode::Avi);
    CHECK(got.opt.codec == app::RecCodec::Zmbv);
    CHECK(got.opt.motion == app::RecMotion::Full);
    CHECK(got.opt.every.count<std::uint16_t>() == 3);
    CHECK(got.opt.from_frame == app::MovieFrame{4} && got.opt.to_frame == app::MovieFrame{9});
    (void)::rmdir(fat.c_str());
    (void)::rmdir(media.c_str());
}

void test_cli_hashes() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--hashes"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && !a->hashes_only);
    const char* const only[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--hashes-only"};
    const auto b = fw::parse_tasty_args(std::span<const char* const>(only));
    CHECK(b.has_value() && b->hashes_only);
}

void test_boot_no_handoff() {
    fw::TastySession::PlayRun run{};
    fw::ReplayMode mode{fw::TastyArgs{}, run};
    const fw::SessionMode::Traits t = mode.traits();
    CHECK(std::string_view{t.boot_args.core}.empty());
    CHECK(!t.boot_args.companion.has_value());
    CHECK(std::string_view{t.program} == "tasty");
    CHECK(t.windows_notice == hal::FallbackNotice::Quiet);

    CHECK(t.stop_ends_ladder);
    CHECK(!t.not_ready_quits_clean);
}

void test_cli_play_needs_rom() {
    const char* const play[] = {"tasty", "play", "m.fm2"};
    CHECK(fw::parse_tasty_args(std::span<const char* const>(play)).has_value());
    const char* const chk[] = {"tasty", "check", "m.fm2"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(chk)).has_value());
}

void test_cli_loop_and_ram_init() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--loop", "--ram-init", "zero"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value() && a->loop && a->ram_fill == cores::IReplayCore::RamFill::Zero);
    const char* const bad[] = {"tasty", "play", "m.fm2", "--ram-init", "nope"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(bad)).has_value());
}

void test_cli_save_is_play_only() {
    const char* const play[] = {"tasty", "play", "m.fm2", "--save", "/s/a.sav"};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(play));
    CHECK(a.has_value() && a->save && a->save->view() == "/s/a.sav");
    const auto refused = [](const auto& argv) {
        const auto d = fw::parse_tasty_args(std::span<const char* const>(argv));
        CHECK(!d.has_value());
        if (!d)
            CHECK(
                std::string_view(fw::TastyRefusal::sentence_for(d.error()))
                == "--save seeds a movie's battery save; use it with tasty play"
            );
    };
    const char* const info[] = {"tasty", "info", "m.fm2", "--save", "/s/a.sav"};
    const char* const check[] = {"tasty", "check", "m.fm2", "--rom", "r.nes", "--save", "/s/a.sav"};
    const char* const status[] = {"tasty", "status", "--save", "/s/a.sav"};
    const char* const stop[] = {"tasty", "stop", "--save", "/s/a.sav"};
    const char* const rec[] = {"tasty", "rec", "start", "--record", "/x", "--save", "/s/a.sav"};
    refused(info);
    refused(check);
    refused(status);
    refused(stop);
    refused(rec);
}

void test_registry() {
    CHECK(fw::tasty_plays("NES"));
    CHECK(fw::tasty_plays("SNES"));
    CHECK(fw::tasty_plays("MegaDrive"));
    CHECK(fw::tasty_plays("PSX"));
    CHECK(fw::tasty_plays("MENU"));
    CHECK(!fw::tasty_plays("AO486"));
    CHECK(!fw::tasty_plays("N64"));

    std::size_t tagged = 0;
    for (const cores::CoreFactory& f : cores::core_table()) {
        if (!f.profile.tasty_playable)
            continue;
        ++tagged;
        CHECK(fw::tasty_plays(f.profile.name));
    }
    CHECK(tagged == 4);
    CHECK(!fw::tasty_plays("SGB"));
}

void test_codec_path() {
    CHECK(cores::movie_codec_for_path("a.fm2").has_value());
    CHECK(cores::movie_codec_for_path("a.lsmv").has_value());
    CHECK(cores::movie_codec_for_path("a.gmv").has_value());
    CHECK(!cores::movie_codec_for_path("a.bk2").has_value());
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
        CHECK(nes->codec.has_value());
        CHECK(cores::movie_system_supported(*nes));
    }
    CHECK(cores::movie_system_for_path("a.lsmv").has_value());
    CHECK(cores::movie_system_for_path("a.gmv").has_value());
    CHECK(!cores::movie_system_for_path("a.bk2").has_value());
    CHECK(cores::movie_system_plays("NES"));
    CHECK(cores::movie_system_plays("PSX"));
    CHECK(!cores::movie_system_plays("MENU"));
    CHECK(!cores::movie_system_plays("N64"));

    bool saw_disk_core = false;
    for (const cores::CoreFactory& f : cores::core_table()) {
        if (f.profile.disk_formats.empty())
            continue;
        saw_disk_core = true;
        CHECK(!cores::movie_system_plays(f.profile.name));
    }
    CHECK(saw_disk_core);
    CHECK(!cores::movie_system_plays("AtariST"));
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
    {
        fw::ReturnHome h{{.probe = true}};
    }
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount::of(1));
    {
        fw::ReturnHome h{{.probe = true, .already_homed = true}};
    }
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount{});
}

void test_return_home_error_path() {
    auto fail = [] {
        fw::ReturnHome h{{.probe = true}};
        return 1;
    };
    CHECK(fail() == 1);
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount::of(1));
}

void test_home_lane_claims_once_per_bind() {
    fw::HomeLane::Bind b{.probe = true};
    CHECK(fw::HomeLane::lane() == fw::HomeLane::Lane::Idle);
    CHECK(fw::HomeLane::may_launch());
    CHECK(fw::HomeLane::LaunchCount{} == fw::HomeLane::LaunchCount::of(0));
    {
        fw::ReturnHome h{b};
        CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount{});
        fw::HomeLane::launch();
        fw::HomeLane::launch();
        CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount::of(1));
    }
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount::of(1));
    {
        fw::ReturnHome h{b};
        CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount{});
    }
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount::of(1));
    b.already_homed = true;
    {
        fw::ReturnHome h{b};
        fw::HomeLane::launch();
    }
    CHECK(fw::HomeLane::launches() == fw::HomeLane::LaunchCount{});
}

void test_run_exit_statuses() {
    CHECK(fw::kSignalBase.v == 128);
    CHECK(std::to_underlying(fw::ExitCode::BusyOwner) == 2);
    CHECK(std::to_underlying(fw::ExitCode::RomMismatch) == 3);
    CHECK(fw::process_status(fw::busy_owner()) == 2);
    CHECK(fw::process_status(fw::rom_mismatch()) == 3);
    CHECK(fw::process_status(fw::refused()) == 1);
    CHECK(fw::process_status(fw::after_signal(os::ChildProcessExit::SignalNumber{SIGTERM})) == 143);
    CHECK(fw::as_exit(fw::busy_owner()) == 2);
    CHECK(fw::as_exit(fw::rom_mismatch()) == 3);
    CHECK(fw::failed(fw::busy_owner()));
    CHECK(fw::failed(fw::rom_mismatch()));
    CHECK(fw::failed(fw::refused()));
    CHECK(!fw::failed(fw::ExitCode::Done));
    infra::JsonOut busy;
    busy.field(infra::JsonName{"exit"}, fw::busy_owner());
    CHECK(busy.view() == "\"exit\":2");
    infra::JsonOut mismatch;
    mismatch.field(infra::JsonName{"exit"}, fw::rom_mismatch());
    CHECK(mismatch.view() == "\"exit\":3");

    infra::JsonOut wedged;
    wedged.field(infra::JsonName{"exit"}, fw::ExitCode::Wedged);
    CHECK(wedged.view() == "\"exit\":75");
}

void test_lock_unlink() {
    Tmp t("/tasty_tasty_lock_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    auto fd = fw::TastyOwnerLock::take(rt.paths);
    CHECK(fd.has_value());
    if (!fd)
        return;
    CHECK(fd->write_pid(*os::ProcessId::of(4242)).has_value());
    const fw::TastyStatusFile status{rt.paths};
    CHECK(status.write("x\n").has_value());
    const auto pid = fw::TastyPidFile{rt.paths}.pid();
    CHECK(pid && pid->printed() == 4242);
    fd->release();
    CHECK(!fw::TastyPidFile{rt.paths}.pid());
    auto fd2 = fw::TastyOwnerLock::take(rt.paths);
    CHECK(fd2.has_value());
    if (fd2)
        fd2->release();
}

void test_two_owner_locks_over_different_paths_coexist() {
    Tmp a("/tasty_tasty_la_");
    Tmp b("/tasty_tasty_lb_");
    CHECK(!a.root.empty() && !b.root.empty());
    if (a.root.empty() || b.root.empty())
        return;
    const fw::TastyRuntime ra = runtime_in(a.root);
    const fw::TastyRuntime rb = runtime_in(b.root);
    auto la = fw::TastyOwnerLock::take(ra.paths);
    auto lb = fw::TastyOwnerLock::take(rb.paths);
    CHECK(la.has_value());
    CHECK(lb.has_value());
    auto again = fw::TastyOwnerLock::take(ra.paths);
    CHECK(!again.has_value());
    if (!again)
        CHECK(again.error().code == Errc::busy);
    if (la)
        la->release();
    if (lb)
        lb->release();
}

void test_status_tmp_path_is_not_clipped() {
    Tmp t("/tasty_tasty_stmp_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    std::string dir = t.root;
    std::vector<std::string> made;
    while (dir.size() < 300) {
        dir += "/" + std::string(48, 's');
        CHECK(::mkdir(dir.c_str(), 0755) == 0);
        made.push_back(dir);
    }
    auto paths = fw::TastyPaths::in(dir);
    CHECK(paths.has_value());
    if (!paths)
        return;
    CHECK(paths->status.size() >= 300);
    CHECK(fw::TastyStatusFile{*paths}.write("x\n").has_value());
    struct stat st{};
    CHECK(::stat(paths->status.c_str(), &st) == 0 && S_ISREG(st.st_mode));
    const auto tmp = paths->status_tmp();
    CHECK(tmp.has_value());
    if (tmp)
        CHECK(::stat(tmp->c_str(), &st) != 0);
    (void)::unlink(paths->status.c_str());
    for (auto it = made.rbegin(); it != made.rend(); ++it)
        (void)::rmdir(it->c_str());

    auto capped = fw::TastyPaths::in("/tmp");
    CHECK(capped.has_value());
    if (!capped)
        return;
    CHECK(capped->status.assign(std::string(1020, 'x')));
    const auto over = capped->status_tmp();
    CHECK(!over.has_value());
    if (!over)
        CHECK(over.error().code == Errc::bad_format);
}

void test_record_dir() {
    CHECK(fw::tasty_record_dir("/media/fat/tasty/cap/") == "/media/fat/tasty/cap");
    CHECK(fw::tasty_record_dir("/media/fat/tasty/cap/contra") == "/media/fat/tasty/cap");
    CHECK(fw::tasty_record_dir("/media/fat/tasty/cap/out.avi") == "/media/fat/tasty/cap");
    CHECK(fw::tasty_record_dir("/out.avi") == "/");
    CHECK(fw::tasty_record_dir("/") == "/");
    CHECK(fw::tasty_record_dir("out.avi") == ".");
}

void test_prepare_record_stem() {
    Tmp t("/tasty_tasty_rec_");
    CHECK(!t.root.empty());
    app::PathText rec{};
    CHECK(rec.assign(t.root));
    const auto out = fw::tasty_prepare_record(rec, "/media/fat/tas/klmz3-smb.fm2");
    CHECK(out.has_value());
    if (!out)
        return;
    CHECK(out->view().find("klmz3-smb") != std::string_view::npos);
    CHECK(out->view().find(t.root) != std::string_view::npos);
}

void test_prepare_record_avi_stem() {
    Tmp t("/tasty_tasty_avi_");
    CHECK(!t.root.empty());
    app::PathText rec{};
    const std::string avi = t.root + "/out.avi";
    CHECK(rec.assign(avi));
    const auto out = fw::tasty_prepare_record(rec, "/media/fat/tas/klmz3-smb.fm2");
    CHECK(out.has_value());
    struct stat st{};
    const bool exists = ::stat(avi.c_str(), &st) == 0;
    CHECK(!exists || !S_ISDIR(st.st_mode));
    if (out)
        CHECK(out->view().size() >= 4 && out->view().substr(out->view().size() - 4) == ".avi");
    if (exists && S_ISDIR(st.st_mode))
        (void)::rmdir(avi.c_str());
    CHECK(t.put("cap", "x"));
    app::PathText file{};
    CHECK(file.assign(t.p("cap")));
    CHECK(!fw::tasty_prepare_record(file, "m.fm2").has_value());
    (void)::unlink(t.p("cap").c_str());
    app::PathText upper{};
    CHECK(upper.assign(t.root + "/Out.AVI"));
    const auto up = fw::tasty_prepare_record(upper, "m.fm2");
    CHECK(up && up->view().size() >= 4 && up->view().substr(up->view().size() - 4) == ".avi");
    const std::string caps = t.root + "/caps";
    app::PathText bare{};
    CHECK(bare.assign(caps));
    const auto dir = fw::tasty_prepare_record(bare, "klmz3-smb.fm2");
    CHECK(dir.has_value());
    struct stat ds{};
    CHECK(::stat(caps.c_str(), &ds) == 0 && S_ISDIR(ds.st_mode));
    if (dir)
        CHECK(dir->view().find("klmz3-smb") != std::string_view::npos);
    (void)::rmdir(caps.c_str());
    if (::mkdir(avi.c_str(), 0755) != 0 && errno != EEXIST)
        CHECK(false);
    app::PathText asdir{};
    CHECK(asdir.assign(avi));
    CHECK(!fw::tasty_prepare_record(asdir, "m.fm2").has_value());
    (void)::rmdir(avi.c_str());
    app::PathText dot{};
    CHECK(dot.assign(t.root + "/.avi"));
    CHECK(!fw::tasty_prepare_record(dot, "m.fm2").has_value());
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
    const auto resolved = fw::tasty_resolve_args(a);
    CHECK(::chdir(old) == 0);
    CHECK(resolved.has_value());
    if (!resolved)
        return;
    CHECK(resolved->movie.view().find(t.root) != std::string_view::npos);
    CHECK(resolved->movie.view().find("m.fm2") != std::string_view::npos);
}

void test_info_missing() {
    Tmp t("/tasty_tasty_info_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    CHECK(fw::tasty_info_movie(*vfs, "missing.fm2") == fw::tasty::Exit::Code::Refused);
}

void test_manifest_doc_role_follows_the_core_set() {
    CHECK(cores::manifest_doc_role().has_value());
}

struct StdoutTo {
    Tmp t{"/tasty_tasty_out_"};
    int saved = -1;

    StdoutTo() {
        std::fflush(stdout);
        saved = ::dup(1);
        const int fd = ::open(t.p("status").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd >= 0) {
            (void)::dup2(fd, 1);
            ::close(fd);
        }
    }

    std::string text() {
        std::fflush(stdout);
        if (saved >= 0) {
            (void)::dup2(saved, 1);
            ::close(saved);
            saved = -1;
        }
        std::string out;
        if (std::FILE* f = std::fopen(t.p("status").c_str(), "rb")) {
            char buf[1024];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                out.append(buf, n);
            std::fclose(f);
        }
        return out;
    }

    ~StdoutTo() { (void)text(); }
};

void test_info_frames_and_rerecords() {
    Tmp t("/tasty_tasty_inf2_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    const std::string
        movie = "version 3\nromChecksum base64:AAAAAAAAAAAAAAAAAAAAAA==\nrerecordCount 5\n"
                "port0 1\nport1 0\nport2 0\n|0|........|||\n|0|........|||\n|0|.......A|||\n";
    CHECK(t.put("m.fm2", movie));
    StdoutTo cap;
    const fw::tasty::Exit rc = fw::tasty_info_movie(*vfs, "m.fm2");
    const std::string out = cap.text();
    CHECK(rc.ok());
    CHECK(out.find("frames 3") != std::string::npos);
    CHECK(out.find("rerecords 5") != std::string::npos);
    (void)::unlink(t.p("m.fm2").c_str());
}

void test_info_reads_an_overlong_header_line_as_the_feeder_does() {
    const std::string rerecord = "rerecordCount 7";
    const auto movie_for = [](const std::string& line) {
        return line + "\nport0 1\nport1 0\nport2 0\n|0|........|||\n";
    };

    {
        Tmp t("/tasty_tasty_ovl_");
        CHECK(!t.root.empty());
        auto vfs = svc::Vfs::create_at(t.root);
        CHECK(vfs.has_value());
        if (!vfs)
            return;
        std::string tail = rerecord;
        tail.append(241, ' ');
        tail.push_back('X');
        CHECK_EQ(tail.size(), 257U);
        const std::string line = "version 3\nromChecksum base64:AAAAAAAAAAAAAAAAAAAAAA==\n" + tail;
        CHECK(t.put("m.fm2", movie_for(line)));
        StdoutTo cap;
        const fw::tasty::Exit rc = fw::tasty_info_movie(*vfs, "m.fm2");
        const std::string out = cap.text();
        CHECK(rc.ok());
        CHECK(out.find("rerecords unknown") != std::string::npos);
        (void)::unlink(t.p("m.fm2").c_str());
    }

    {
        Tmp t("/tasty_tasty_ovl2_");
        CHECK(!t.root.empty());
        auto vfs = svc::Vfs::create_at(t.root);
        CHECK(vfs.has_value());
        if (!vfs)
            return;
        std::string tail = rerecord;
        tail.append(242, ' ');
        tail.push_back('X');
        CHECK_EQ(tail.size(), 258U);
        const std::string line = "version 3\nromChecksum base64:AAAAAAAAAAAAAAAAAAAAAA==\n" + tail;
        CHECK(t.put("m.fm2", movie_for(line)));
        StdoutTo cap;
        const fw::tasty::Exit rc = fw::tasty_info_movie(*vfs, "m.fm2");
        const std::string out = cap.text();
        CHECK(rc.ok());
        CHECK(out.find("rerecords 7") != std::string::npos);
        (void)::unlink(t.p("m.fm2").c_str());
    }
}

void test_info_reads_an_unterminated_last_header_line() {
    Tmp t("/tasty_tasty_tail_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    CHECK(t.put(
        "m.fm2",
        std::string("version 3\n")
            + "romChecksum base64:AAAAAAAAAAAAAAAAAAAAAA==\n"
            + "port0 1\n"
            + "port1 0\n"
            + "port2 0\n"
            + "rerecordCount 9"
    ));
    StdoutTo cap;
    const fw::tasty::Exit rc = fw::tasty_info_movie(*vfs, "m.fm2");
    const std::string out = cap.text();
    CHECK(rc.ok());
    CHECK(out.find("rerecords 9") != std::string::npos);
    (void)::unlink(t.p("m.fm2").c_str());
}

void test_check_missing() {
    Tmp t("/tasty_tasty_ckm_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    CHECK(
        fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "missing.fm2", .rom = "r.nes"})
        == fw::tasty::Exit::Code::Refused
    );
}

void test_busy_text() {
    Tmp t("/tasty_tasty_busy_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    auto lock = fw::TastyOwnerLock::take(rt.paths);
    CHECK(lock.has_value());
    if (lock) {
        const auto wrote = lock->write_pid(*os::ProcessId::of(99), "/media/fat/tas/smb.fm2");
        CHECK(wrote.has_value());
    }
    const std::string msg = fw::TastyPidFile{rt.paths}.busy_text();
    CHECK(msg.find("smb.fm2") != std::string::npos);
    CHECK(msg.find("99") != std::string::npos);
    if (lock)
        lock->release();
}

void test_stop_idle() {
    Tmp t("/tasty_tasty_idle_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    const fw::TastyClient client{rt.paths, rt.owner};
    CHECK(!client.owner_live());
}

void test_stop_owner_signaled() {
    Tmp t("/tasty_tasty_sig_");
    CHECK(!t.root.empty());
    fw::TastyRuntime rt = runtime_in(t.root);
    char comm[32]{};
    const int fd = ::open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    if (fd >= 0) {
        const ssize_t n = ::read(fd, comm, sizeof comm - 1);
        ::close(fd);
        if (n > 0 && comm[n - 1] == '\n')
            comm[n - 1] = '\0';
    }
    const auto owner = os::ProcessName::of(comm);
    CHECK(owner.has_value());
    if (!owner)
        return;
    rt.owner = *owner;
    auto lock = fw::TastyOwnerLock::take(rt.paths);
    CHECK(lock.has_value());
    if (lock)
        CHECK(lock->write_pid(os::ProcessId::self()).has_value());
    const fw::TastyClient client{rt.paths, rt.owner};
    CHECK(client.owner_live());
    (void)::unlink(rt.paths.pid.c_str());
    if (lock)
        lock->release();
}

void test_rec_start_checks_the_owner() {
    Tmp t("/tasty_tasty_rec_");
    CHECK(!t.root.empty());
    fw::TastyRuntime rt = runtime_in(t.root);
    auto lock = fw::TastyOwnerLock::take(rt.paths);
    CHECK(lock.has_value());
    if (lock)
        CHECK(lock->write_pid(os::ProcessId::self()).has_value());
    const fw::TastyClient client{rt.paths, rt.owner};
    CHECK(!client.joins_owner());
    char comm[32]{};
    const int fd = ::open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        const ssize_t n = ::read(fd, comm, sizeof comm - 1);
        ::close(fd);
        if (n > 0 && comm[n - 1] == '\n')
            comm[n - 1] = '\0';
    }
    const auto owner = os::ProcessName::of(comm);
    CHECK(owner.has_value());
    if (!owner)
        return;
    rt.owner = *owner;
    const fw::TastyClient named{rt.paths, rt.owner};
    CHECK(named.joins_owner());
    (void)::unlink(rt.paths.pid.c_str());
    CHECK(!named.joins_owner());
    if (lock)
        lock->release();
}

void test_stop_stale_pid() {
    Tmp t("/tasty_tasty_stale_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    auto lock = fw::TastyOwnerLock::take(rt.paths);
    CHECK(lock.has_value());
    if (lock)
        CHECK(lock->write_pid(*os::ProcessId::of(1)).has_value());
    const fw::TastyClient client{rt.paths, rt.owner};
    CHECK(!client.owner_live());
    (void)::unlink(rt.paths.pid.c_str());
    if (lock)
        lock->release();
}

void test_load_edge_rereads_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump)
        return;
    app::IdentityLatch ident{};
    publish_id(ident, "NES");
    fw::TastySink sink{*r.pump, infra::OptRef<const app::IdentityLatch>{ident}};
    r.core(1, 1'671'559);
    auto g = r.tick();
    CHECK(g.valid);
    CHECK(g.rate == app::FrameTicks::plausible(1'671'559));
    CHECK_EQ(g.core_seq, 1u);
    {
        const SeatScope ui{SeatTag::Ui};
        r.pump->on_pause();
    }
    r.core(3, 1'663'934);
    g = r.tick();

    CHECK(g.rate == app::FrameTicks::plausible(1'671'559));
    {
        const SeatScope ui{SeatTag::Ui};
        sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    }
    for (int i = 0; i < 3; ++i)
        g = r.tick();
    CHECK(g.valid);
    CHECK(g.rate == app::FrameTicks::plausible(1'663'934));
    CHECK_EQ(g.core_seq, 3u);
    CHECK_EQ(r.pump->stats().edges, 1u);
}

void test_unhandled_events_are_counted_silently() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump)
        return;
    fw::TastySink sink{*r.pump, {}};
    const SeatScope ui{SeatTag::Ui};
    sink.deliver(infra::make<app::Event>(app::Event::SdActivity{}, app::Event::Head{}));
    sink.deliver(infra::make<app::Event>(app::Event::InfoRequest{}, app::Event::Head{}));
    CHECK_EQ(sink.event_unhandled_drops(), 2u);
    CHECK_EQ(sink.event_misrouted(), 0u);
}

void test_start_waits_for_video_edge() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump)
        return;
    fw::TastySink sink{*r.pump, {}};
    Sess x;
    x.w.video = infra::OptRef{*r.pump};
    x.s.emplace(x.w, x.args, x.play_run);
    x.tick();
    publish_id(x.ident, "NES");
    for (int i = 0; i < 3; ++i)
        x.tick();
    CHECK_EQ(x.replay.plays, 0u);
    sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    (void)r.tick();
    x.tick();
    x.tick();
    CHECK_EQ(x.replay.plays, 1u);
}

void test_take_play_refusal() {
    Sess x;
    x.replay.play_ok = false;
    to_run(x);
    CHECK(x.replay.plays == 1);
    CHECK(x.exit_code() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_a_port_that_forgets_a_verb_is_abstract() {
    struct Full final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoTakePlay final : fw::IReplayPort {
        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoTakeStop final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoMovieFrames final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoActive final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoRefusal final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        SettingNo settings_set() const noexcept override { return {}; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoSettingsSet final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        std::string setting_line(SettingNo) const override { return {}; }
    };

    struct NoSettingLine final : fw::IReplayPort {
        bool take_play(const app::ReplayFeeder::Play&) noexcept override { return true; }

        void take_stop() noexcept override {}

        app::FrameCount movie_frames() const noexcept override { return app::FrameCount{}; }

        bool active() const noexcept override { return true; }

        std::optional<fw::ReplayRefusal> refusal() const noexcept override { return std::nullopt; }

        SettingNo settings_set() const noexcept override { return {}; }
    };

    static_assert(std::is_abstract_v<fw::IReplayPort>);
    static_assert(!std::is_abstract_v<Full>);
    static_assert(std::is_abstract_v<NoTakePlay>);
    static_assert(std::is_abstract_v<NoTakeStop>);
    static_assert(std::is_abstract_v<NoMovieFrames>);
    static_assert(std::is_abstract_v<NoActive>);
    static_assert(std::is_abstract_v<NoRefusal>);
    static_assert(std::is_abstract_v<NoSettingsSet>);
    static_assert(std::is_abstract_v<NoSettingLine>);
}

void test_a_core_ask_on_a_full_ring_is_retried() {
    Sess x;
    unsigned filled = 0;
    while (x.asks.push(app::UiRequest::LoadCore{}))
        ++filled;
    CHECK(filled > 0u);
    x.tick();
    unsigned drained = 0;
    while (x.load_asked())
        ++drained;
    CHECK_EQ(drained, filled);
    CHECK(!x.load_asked());
    x.tick();
    CHECK(x.load_asked());
    CHECK_EQ(x.exit_code(), 0);
}

void test_a_taken_core_ask_is_not_repeated() {
    Sess x;
    x.tick();
    CHECK(x.load_asked());
    x.tick();
    CHECK(!x.load_asked());
}

void test_a_menu_ask_on_a_full_ring_is_retried() {
    Sess x;
    to_run(x);
    while (x.load_asked()) {
    }
    unsigned filled = 0;
    while (x.asks.push(app::UiRequest::LoadCore{}))
        ++filled;
    CHECK(filled > 0u);
    x.stop.store(1);
    x.tick();
    unsigned drained = 0;
    while (x.load_asked())
        ++drained;
    CHECK_EQ(drained, filled);
    x.tick();
    CHECK(x.load_asked());
}

void test_later_refusal_ends_session() {
    Sess x;
    to_run(x);
    CHECK(x.replay.plays == 1);
    CHECK(x.exit_code() == 0);
    x.replay.ref = fw::ReplayRefusal{.why = app::PlayRefusal::Why::Setting};
    x.tick();
    CHECK(x.exit_code() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_epoch_ambiguous_then_finished() {
    Sess x;
    to_run(x);
    CHECK(x.replay.plays == 1);
    publish_end(x.play, app::ReplayEnd::EpochAmbiguous);
    x.tick();
    CHECK(x.replay.stops == 0);
    CHECK(x.exit_code() == 0);
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
    CHECK(x.exit_code() == 0);
}

void test_epoch_ambiguous_final_leaves() {
    Sess x;
    to_run(x);
    publish_end(x.play, app::ReplayEnd::EpochAmbiguous);
    x.tick();
    CHECK(x.exit_code() == 0);
    x.replay.feeding = false;
    x.tick();
    CHECK(x.exit_code() == 1);
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_replay_ends_leave() {
    const app::ReplayEnd ends[] = {
        app::ReplayEnd::Finished,
        app::ReplayEnd::Stopped,
        app::ReplayEnd::Superseded,
        app::ReplayEnd::CoreSwitch,
        app::ReplayEnd::RefLost,
        app::ReplayEnd::NoReference,
        app::ReplayEnd::Refused
    };
    for (const auto end : ends) {
        Sess x;
        to_run(x);
        CHECK(x.replay.plays == 1);
        publish_end(x.play, end);
        x.tick();
        if (end == app::ReplayEnd::Finished)
            x.tick();
        home(x);
        CHECK(x.replay.stops >= 1);
        if (end != app::ReplayEnd::Finished && end != app::ReplayEnd::Stopped) {
            CHECK(x.exit_code() == 1);
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

void test_replay_end_sentences() {
    const app::ReplayEnd ends[] = {
        app::ReplayEnd::Superseded,
        app::ReplayEnd::CoreSwitch,
        app::ReplayEnd::RefLost,
        app::ReplayEnd::NoReference,
        app::ReplayEnd::EpochAmbiguous,
        app::ReplayEnd::Refused
    };
    for (const auto end : ends) {
        StderrTo err;
        Sess x;
        to_run(x);
        publish_end(x.play, end);
        x.replay.feeding = false;
        x.tick();
        const std::string text = err.text();
        CHECK(text.find("replay ended") == std::string::npos);
        CHECK(text.find("tasty: ") != std::string::npos);
        CHECK(text.find(app::replay_end_sentence(end)) != std::string::npos);
    }

    const std::string untimed = app::replay_end_sentence(
        app::ReplayEnd::EpochAmbiguous,
        app::EpochFail::Untimed
    );
    CHECK(untimed.find("--lead 0") != std::string::npos);
    for (const auto f : {app::EpochFail::None, app::EpochFail::Retry, app::EpochFail::Before}) {
        const std::string other = app::replay_end_sentence(app::ReplayEnd::EpochAmbiguous, f);
        CHECK(other.find("--lead") == std::string::npos);
        CHECK(other.find("play it again") != std::string::npos);
    }
    StderrTo err;
    Sess x;
    to_run(x);
    {
        const SeatScope rt{SeatTag::RT};
        app::ReplayStatus st{};
        st.level = app::ReplayLevel::Idle;
        st.end = app::ReplayEnd::EpochAmbiguous;
        st.epoch_fail = app::EpochFail::Untimed;
        x.play.publish(st);
    }
    x.replay.feeding = false;
    x.tick();
    CHECK(err.text().find(untimed) != std::string::npos);
}

void test_prepare_record_says_why() {
    Tmp t("/tasty_tasty_avi2_");
    CHECK(!t.root.empty());
    CHECK(t.put("cap", "x"));
    StderrTo err;
    app::PathText file{};
    CHECK(file.assign(t.p("cap")));
    CHECK(!fw::tasty_prepare_record(file, "m.fm2").has_value());
    const std::string text = err.text();
    CHECK(text.find("tasty: ") != std::string::npos);
    CHECK(text.find("file") != std::string::npos);
    (void)::unlink(t.p("cap").c_str());
}

void test_loop_restarts_after_finish() {
    Sess x;
    x.args.loop = true;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    CHECK_EQ(x.replay.plays, 1u);
    while (x.load_asked()) {
    }
    publish_end(x.play, app::ReplayEnd::Finished);
    x.replay.feeding = true;
    x.tick();
    CHECK_EQ(x.replay.plays, 1u);
    x.replay.feeding = false;
    x.tick();
    CHECK_EQ(x.replay.plays, 1u);
    x.tick();
    CHECK_EQ(x.replay.plays, 2u);
    CHECK_EQ(x.replay.stops, 0u);
    CHECK(!x.load_asked());
    x.stop.store(1);
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_loop_wins_over_stay() {
    Sess x;
    x.args.loop = true;
    x.args.stay = true;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    x.replay.feeding = false;
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    x.tick();
    CHECK_EQ(x.replay.plays, 2u);
    CHECK_EQ(x.replay.stops, 0u);
}

void test_session_passes_ram_init() {
    Sess x;
    x.args.ram_fill = cores::IReplayCore::RamFill::Random;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    CHECK(x.replay.last.ram_fill == cores::IReplayCore::RamFill::Random);
}

void test_stay_finished_holds() {
    Sess x;
    x.args.stay = true;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    CHECK(x.replay.stops == 0);
}

void test_stay_stop_leaves() {
    Sess x;
    x.args.stay = true;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    x.stop.store(1);
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_rec_only_stop_leaves() {
    Sess x;
    x.args.no_splash = true;
    x.s.emplace(x.w, x.args, x.rec_run);
    x.tick();
    CHECK(x.replay.plays == 0);
    x.stop.store(1);
    x.tick();
    home(x);
    CHECK(x.replay.stops >= 1);
}

void test_rec_only_leaves_when_its_recording_ends() {
    for (const bool refused : {false, true}) {
        Sess x;
        x.args.no_splash = true;
        x.args.record.emplace();
        (void)x.args.record->assign("/tmp/cap");
        x.s.emplace(x.w, x.args, x.rec_run);
        x.tick();
        x.tick();
        if (refused) {
            x.capture_level(
                app::RecState::Idle,
                app::RecVerdict::NoLiveBuffer,
                app::RecVerdict::NoLiveBuffer
            );
        } else {
            x.capture_level(app::RecState::Recording, app::RecVerdict::Started);
            for (int i = 0; i < 4; ++i)
                x.tick();
            CHECK(x.replay.stops == 0);
            x.capture_level(
                app::RecState::Idle,
                app::RecVerdict::Stopped,
                app::RecVerdict::Stopped
            );
        }
        x.tick();
        home(x);
        CHECK(x.replay.stops >= 1);
        CHECK(x.exit_code() == (refused ? 1 : 0));
    }
}

void test_rec_only_reports_a_failure_after_it_started() {
    Sess x;
    x.args.no_splash = true;
    x.args.record.emplace();
    (void)x.args.record->assign("/tmp/cap");
    x.s.emplace(x.w, x.args, x.rec_run);
    StderrTo err;
    x.tick();
    x.capture_level(app::RecState::Recording, app::RecVerdict::Started);
    for (int i = 0; i < 4; ++i)
        x.tick();
    CHECK(x.replay.stops == 0);
    x.capture_level(app::RecState::Idle, app::RecVerdict::None, app::RecVerdict::WriterFailed);
    x.tick();
    x.tick();
    home(x);
    const std::string said = err.text();
    CHECK(x.replay.stops >= 1);
    CHECK(x.exit_code() == 1);
    const std::string line = "the recording failed after it started (writer_failed)";
    const auto first = said.find(line);
    CHECK(first != std::string::npos);
    CHECK(first == std::string::npos || said.find(line, first + 1) == std::string::npos);
}

void test_rec_only_a_stopped_recording_after_it_started_is_clean() {
    Sess x;
    x.args.no_splash = true;
    x.args.record.emplace();
    (void)x.args.record->assign("/tmp/cap");
    x.s.emplace(x.w, x.args, x.rec_run);
    StderrTo err;
    x.tick();
    x.capture_level(app::RecState::Recording, app::RecVerdict::Started);
    for (int i = 0; i < 4; ++i)
        x.tick();
    x.capture_level(app::RecState::Idle, app::RecVerdict::Stopped, app::RecVerdict::Stopped);
    x.tick();
    home(x);
    const std::string said = err.text();
    CHECK(x.replay.stops >= 1);
    CHECK(x.exit_code() == 0);
    CHECK(said.find("the recording failed after it started") == std::string::npos);
}

void test_a_recorded_replay_reports_a_failure_while_it_runs() {
    Tmp t("/tasty_tasty_recrep_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    Sess x;
    app::PathText cap{};
    CHECK(cap.assign(t.p("cap")));
    x.args.record = cap;
    x.s.emplace(x.w, x.args, x.play_run);
    StderrTo err;
    to_run(x);
    CHECK(x.replay.plays == 1);
    x.capture_level(app::RecState::Recording, app::RecVerdict::Started);
    for (int i = 0; i < 4; ++i)
        x.tick();
    CHECK(x.replay.stops == 0);
    CHECK(x.exit_code() == 0);
    x.capture_level(app::RecState::Idle, app::RecVerdict::None, app::RecVerdict::WriterFailed);
    for (int i = 0; i < 10; ++i)
        x.tick();
    CHECK(x.replay.stops == 0);
    CHECK(x.exit_code() == 1);
    const std::string said = err.text();
    const std::string line = "the recording failed after it started (writer_failed)";
    const auto first = said.find(line);
    CHECK(first != std::string::npos);
    CHECK(first == std::string::npos || said.find(line, first + 1) == std::string::npos);
}

void test_a_recorded_replay_reports_a_failure_as_it_closes() {
    Tmp t("/tasty_tasty_recrepc_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    Sess x;
    app::PathText cap{};
    CHECK(cap.assign(t.p("cap")));
    x.args.record = cap;
    x.s.emplace(x.w, x.args, x.play_run);
    StderrTo err;
    to_run(x);
    x.capture_level(app::RecState::Recording, app::RecVerdict::Started);
    x.tick();
    publish_end(x.play, app::ReplayEnd::Finished);
    x.tick();
    CHECK(x.exit_code() == 0);
    x.capture_level(app::RecState::Idle, app::RecVerdict::None, app::RecVerdict::WriterFailed);
    x.tick();
    home(x);
    CHECK(x.exit_code() == 1);
    const std::string said = err.text();
    const std::string line = "the recording failed after it started (writer_failed)";
    const auto first = said.find(line);
    CHECK(first != std::string::npos);
    CHECK(first == std::string::npos || said.find(line, first + 1) == std::string::npos);
}

void test_rec_only_probe_refusal_fails_the_run() {
    for (const auto v : {app::RecVerdict::NoLiveBuffer, app::RecVerdict::ScalerPortStuck}) {
        Sess x;
        x.args.no_splash = true;
        x.args.record.emplace();
        (void)x.args.record->assign("/tmp/cap");
        x.s.emplace(x.w, x.args, x.rec_run);
        StderrTo err;
        x.tick();
        x.tick();
        x.capture_level(app::RecState::Probing, app::RecVerdict::None);
        for (int i = 0; i < 3; ++i)
            x.tick();
        CHECK(x.replay.stops == 0);
        x.capture_level(app::RecState::Idle, v, v);
        x.tick();
        home(x);
        const std::string said = err.text();
        CHECK(x.replay.stops >= 1);
        CHECK(x.exit_code() == 1);
        if (v == app::RecVerdict::NoLiveBuffer) {
            CHECK(said.find("did not start (no_live_buffer)") != std::string::npos);
        } else {
            CHECK(said.find("reboot the MiSTer to clear it") != std::string::npos);
        }
    }
}

void test_a_stuck_port_ends_a_replay() {
    Sess x;
    x.args.stay = true;
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    StderrTo err;
    x.capture_level(app::RecState::Idle, app::RecVerdict::None, app::RecVerdict::ScalerPortStuck);
    for (int i = 0; i < 4; ++i)
        x.tick();
    home(x);
    const std::string said = err.text();
    CHECK(x.replay.stops >= 1);
    CHECK(x.exit_code() == 1);
    const std::string remedy = app::rec_verdict_remedy(app::RecVerdict::ScalerPortStuck);
    const auto first = said.find(remedy);
    CHECK(first != std::string::npos);
    CHECK(first == std::string::npos || said.find(remedy, first + 1) == std::string::npos);
    CHECK(app::rec_verdict_remedy(app::RecVerdict::NoLiveBuffer) == nullptr);
}

void test_the_verdict_line_names_the_remedy() {
    Tmp t("/tasty_tasty_remedy_");
    CHECK(!t.root.empty());
    xthread::DiagLog diag;
    CHECK(diag.open(t.p("diag").c_str()));
    app::RecorderStatusCell status{};
    app::RecControlChannel control{xthread::Polled{}, xthread::Polled{}};
    auto capture = control.worker();
    app::RecorderControl rc{app::RecorderControl::Wiring{
        .control = infra::OptRef<app::RecControlChannel>{control},
        .status = infra::OptRef<const app::RecorderStatusCell>{status},
        .diag = infra::OptRef<xthread::DiagLog>{diag}
    }};
    {
        const SeatScope ui{SeatTag::Ui};
        CHECK(rc.take_stop());
    }
    {
        const SeatScope cap{SeatTag::Capture};
        auto job = capture.take();
        CHECK(job);
        if (job) {
            job->verdict = app::RecVerdict::ScalerPortStuck;
            job.complete();
        }
    }
    {
        const SeatScope ui{SeatTag::Ui};
        rc.tick();
    }
    std::string line;
    if (std::FILE* f = std::fopen(t.p("diag").c_str(), "rb")) {
        char buf[4096];
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        line.assign(buf, n);
        std::fclose(f);
    }
    CHECK(line.find("\"v\":\"scaler_port_stuck\"") != std::string::npos);
    CHECK(line.find("\"remedy\":\"") != std::string::npos);
    CHECK(line.find("reboot the MiSTer to clear it") != std::string::npos);
}

void test_record_path_outside_the_root_is_refused() {
    Tmp t("/tasty_tasty_recroot_");
    CHECK(!t.root.empty());
    app::RecorderControl rc{app::RecorderControl::Wiring{.root = t.root}};
    Sess x{nullptr, nullptr, {.rec = &rc}};
    x.args.no_splash = true;
    CHECK(x.args.record.emplace().assign("/proc/"));
    x.s.emplace(x.w, x.args, x.rec_run);
    StderrTo err;
    x.tick();
    x.tick();
    home(x);
    CHECK(x.exit_code() == 1);
    CHECK(err.text().find("cannot record to /proc/") != std::string::npos);
    CHECK(rc.generation() == app::ControlGeneration{});
}

void test_stale_generation() {
    Tmp t("/tasty_tasty_gen_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    Sess x{&*vfs};
    x.args = play_args();
    (void)x.args.movie.assign("m.fm2");
    x.s.emplace(x.w, x.args, x.play_run);
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

namespace {
std::optional<std::string> slurp(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return std::nullopt;
    std::string b;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
        b.append(buf, n);
    std::fclose(f);
    return b;
}

struct SeedRun {
    unsigned plays = 0;
    bool seeded = false;
    int exit_code = -1;
    std::optional<std::string> seed{};
};

SeedRun seed_run(const Tmp& t, const char* save) {
    SeedRun out{};
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return out;
    Sess x{&*vfs};
    x.args.save.emplace();
    (void)x.args.save->assign(save);
    x.s.emplace(x.w, x.args, x.play_run);
    to_run(x);
    out.plays = x.replay.plays;
    out.seeded = x.replay.last.seeded_save;
    out.exit_code = x.exit_code();
    out.seed = slurp(t.p("tasty/saves/seed.sav"));
    (void)::unlink(t.p("tasty/saves/seed.sav").c_str());
    (void)::rmdir(t.p("tasty/saves").c_str());
    (void)::rmdir(t.p("tasty").c_str());
    return out;
}
}

void test_save_seed_copied() {
    Tmp t("/tasty_tasty_seed_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    std::string bytes(8192, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(i * 7u + 3u);
    CHECK(t.put("my.sav", bytes));
    const SeedRun r = seed_run(t, "my.sav");
    CHECK_EQ(r.plays, 1u);
    CHECK(r.seeded);
    CHECK_EQ(r.exit_code, 0);
    CHECK(r.seed && *r.seed == bytes);
    CHECK(slurp(t.p("my.sav")) == bytes);

    CHECK(t.put("empty.sav", ""));
    const SeedRun e = seed_run(t, "empty.sav");
    CHECK_EQ(e.plays, 1u);
    CHECK(e.seeded);
    CHECK(e.seed && e.seed->empty());
    (void)::unlink(t.p("my.sav").c_str());
    (void)::unlink(t.p("empty.sav").c_str());
}

void test_save_seed_refusals() {
    Tmp t("/tasty_tasty_seedno_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    const SeedRun missing = seed_run(t, "absent.sav");
    CHECK_EQ(missing.plays, 0u);
    CHECK_EQ(missing.exit_code, 1);
    CHECK(!missing.seed);

    CHECK(t.put("max.sav", std::string(1u << 20, 'm')));
    const SeedRun max = seed_run(t, "max.sav");
    CHECK_EQ(max.plays, 1u);
    CHECK(max.seed && max.seed->size() == (1u << 20));
    CHECK(t.put("big.sav", std::string((1u << 20) + 1u, 'b')));
    const SeedRun big = seed_run(t, "big.sav");
    CHECK_EQ(big.plays, 0u);
    CHECK_EQ(big.exit_code, 1);
    CHECK(!big.seed);

    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (vfs) {
        Sess x{&*vfs};
        to_run(x);
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(!x.replay.last.seeded_save);
        CHECK(!slurp(t.p("tasty/saves/seed.sav")));
    }
    (void)::unlink(t.p("max.sav").c_str());
    (void)::unlink(t.p("big.sav").c_str());
}

void test_status_force() {
    Tmp t("/tasty_tasty_st_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    const std::string st = t.p("status");
    Sess x{nullptr, nullptr, Sess::Binds{}, rt};
    to_run(x);
    x.tick();
    const auto first = fw::TastyClient{rt.paths, rt.owner}.status();
    CHECK(first.has_value());
    publish_end(x.play, app::ReplayEnd::Finished);
    struct stat a{};
    struct stat b{};
    CHECK(::stat(st.c_str(), &a) == 0);
    x.tick();
    CHECK(::stat(st.c_str(), &b) == 0);
    CHECK(a.st_mtime == b.st_mtime && a.st_size == b.st_size);
    x.tick();
    home(x);
    auto last = fw::TastyClient{rt.paths, rt.owner}.status();
    CHECK(last.has_value());
    if (last)
        CHECK(last->find("finished") != std::string::npos);
}

void test_status_frames_total() {
    Tmp t("/tasty_tasty_fr_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    const std::string st = t.p("status");
    Sess x{nullptr, nullptr, Sess::Binds{}, rt};
    to_run(x);
    x.tick();
    const auto s = fw::TastyClient{rt.paths, rt.owner}.status();
    CHECK(s.has_value());
    if (s)
        CHECK(s->find("\"frames\":12") != std::string::npos);
    home(x);
}

void expect_scale_sentence(std::uint8_t fell, const char* warn, const char* needle) {
    Tmp t("/tasty_tasty_sc_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    const fw::TastyRuntime rt = runtime_in(t.root);
    const std::string st = t.p("status");
    app::EncodeStatusCell enc{};
    Sess x{nullptr, nullptr, {.encstat = &enc}, rt};
    {
        const SeatScope enc_seat{SeatTag::Encode};
        app::EncodeStatus body{};
        body.video.scale = app::PictureScale::Half;
        body.video.steps = app::StepCount{1};
        body.video.fell_behind = fell;
        enc.publish(body);
    }
    to_run(x);
    std::string err;
    {
        StderrTo cap;
        x.tick();
        err = cap.text();
    }
    const auto s = fw::TastyClient{rt.paths, rt.owner}.status();
    CHECK(s.has_value());
    if (s)
        CHECK(s->find("\"scale\":2") != std::string::npos);
    CHECK(err == warn);
    publish_end(x.play, app::ReplayEnd::Finished);
    std::string again;
    {
        StderrTo cap;
        x.tick();
        x.tick();
        home(x);
        again = cap.text();
    }
    CHECK(again.find(needle) == std::string::npos);
}

void test_status_scale_step_warns_per_recording() {
    Tmp t("/tasty_tasty_sg_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    const fw::TastyRuntime rt = runtime_in(t.root);
    const std::string st = t.p("status");
    app::EncodeStatusCell enc{};
    Sess x{nullptr, nullptr, {.encstat = &enc}, rt};
    const auto step = [&](std::uint16_t gen) {
        {
            const SeatScope cap{SeatTag::Capture};
            app::RecorderStatus r{};
            r.gen = app::ControlGeneration{gen};
            x.recstat.publish(r);
        }
        const SeatScope enc_seat{SeatTag::Encode};
        app::EncodeStatus body{};
        body.video.scale = app::PictureScale::Half;
        body.video.steps = app::StepCount{1};
        enc.publish(body);
    };
    const auto said = [&] {
        ::usleep(260 * 1000);
        StderrTo cap;
        x.tick();
        return cap.text().find("too much CPU") != std::string::npos;
    };
    to_run(x);
    step(1);
    CHECK(said());
    CHECK(!said());
    step(2);
    CHECK(said());
    CHECK(!said());
    publish_end(x.play, app::ReplayEnd::Finished);
    {
        StderrTo cap;
        x.tick();
        home(x);
    }
}

void test_status_scale_step_warns_once() {
    const char* tail = " (--scale native keeps full size and may repeat frames instead)\n";
    const std::string
        cpu = std::string(
                  "tasty: the encoder was using too much CPU; the AVI continues at half size in "
                  "a new segment"
              )
        + tail;
    const std::string
        behind = std::string(
                     "tasty: the encoder fell behind; the AVI continues at half size in a new "
                     "segment until it catches up"
                 )
        + tail;
    expect_scale_sentence(0, cpu.c_str(), "too much CPU");
    expect_scale_sentence(1, behind.c_str(), "fell behind");
}

void test_status_scale_return_is_said() {
    Tmp t("/tasty_tasty_sr_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    const fw::TastyRuntime rt = runtime_in(t.root);
    const std::string st = t.p("status");
    app::EncodeStatusCell enc{};
    Sess x{nullptr, nullptr, {.encstat = &enc}, rt};
    const auto publish = [&](std::uint8_t steps, std::uint8_t recovered, std::uint8_t held = 0) {
        const SeatScope enc_seat{SeatTag::Encode};
        app::EncodeStatus body{};
        body.video.scale = steps > recovered ? app::PictureScale::Half : app::PictureScale::Native;
        body.video.steps = app::StepCount{steps};
        body.video.recovered = app::StepCount{recovered};
        body.video.fell_behind = 1;
        body.video.held = held;
        enc.publish(body);
    };
    const std::string
        back = "tasty: the encoder caught up; the AVI is back at full size in a new segment\n";
    const auto ends_with = [](const std::string& text, const std::string& tail) {
        return text.size() >= tail.size()
            && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
    };
    const auto said = [&] {
        ::usleep(260 * 1000);
        StderrTo cap;
        x.tick();
        return cap.text();
    };
    to_run(x);
    publish(1, 0);
    CHECK(said().find("fell behind") != std::string::npos);
    publish(1, 1);
    CHECK(
        said() == "tasty: the encoder caught up; the AVI is back at full size in a new segment\n"
    );
    CHECK(said().empty());
    publish(2, 1);
    CHECK(said().find("fell behind") != std::string::npos);

    publish(3, 2);
    std::string both = said();
    CHECK(both.find(back) == 0);
    CHECK(both.find("until it catches up") != std::string::npos);
    publish(4, 4);
    both = said();
    CHECK(both.find("tasty: the encoder fell behind;") == 0);
    CHECK(ends_with(both, back));

    publish(5, 4, 1);
    const std::string held = said();
    CHECK(held.find("fell behind again soon after catching up") != std::string::npos);
    CHECK(held.find("until it catches up") == std::string::npos);
    publish_end(x.play, app::ReplayEnd::Finished);
    {
        StderrTo cap;
        x.tick();
        home(x);
    }
}

void test_check_exits() {
    Tmp t("/tasty_tasty_ck_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    std::string gmv(64, '\0');
    std::memcpy(gmv.data(), "Gens Movie TEST", 15);
    gmv[0x0F] = 'A';
    gmv[0x14] = '6';
    gmv[0x15] = '6';
    CHECK(t.put("none.gmv", gmv));
    CHECK(fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "none.gmv", .rom = "r.nes"}).ok());
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
        const std::uint32_t n = (std::uint32_t{d[i]} << 16)
            | (i + 1 < 16 ? std::uint32_t{d[i + 1]} << 8 : 0u)
            | (i + 2 < 16 ? std::uint32_t{d[i + 2]} : 0u);
        b64 += k[(n >> 18) & 63];
        b64 += k[(n >> 12) & 63];
        b64 += i + 1 < 16 ? k[(n >> 6) & 63] : '=';
        b64 += i + 2 < 16 ? k[n & 63] : '=';
    }
    const std::string good = "version 3\n"
        + b64
        + "\nromFilename x\nguid 1\nport0 1\nport1 0\nport2 0\n"
          "fourscore 0\nFDS 0\n|0|........|\n";
    CHECK(t.put("good.fm2", good));
    CHECK(fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "good.fm2", .rom = "r.nes"}).ok());
    rom[20] ^= 0xFF;
    CHECK(
        t.put("bad.nes", std::string_view(reinterpret_cast<const char*>(rom.data()), rom.size()))
    );
    CHECK(
        fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "good.fm2", .rom = "bad.nes"})
        == fw::tasty::Exit::Code::Mismatch
    );

    CHECK(t.put("r.chd", std::string(4096, 'x')));
    {
        std::string err;
        fw::tasty::Exit rc;
        {
            StderrTo cap;
            rc = fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "good.fm2", .rom = "r.chd"});
            err = cap.text();
        }
        CHECK(rc == fw::tasty::Exit::Code::Refused);
        CHECK(err == "tasty: cannot hash rom r.chd\n");
    }
    (void)::unlink(t.p("r.chd").c_str());
    (void)::unlink(t.p("none.gmv").c_str());
    (void)::unlink(t.p("good.fm2").c_str());
    (void)::unlink(t.p("bad.nes").c_str());
}

bool write_bk2_text(const std::string& path, const std::string& header, const std::string& log) {
    zipFile zf = ::zipOpen64(path.c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr)
        return false;
    const char* names[] = {"Header.txt", "Input Log.txt"};
    const std::string bodies[] = {header, log};
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        if (::zipOpenNewFileInZip(zf, names[i], nullptr, nullptr, 0, nullptr, 0, nullptr, 0, 0)
            != ZIP_OK) {
            ok = false;
            break;
        }
        if (::zipWriteInFileInZip(zf, bodies[i].data(), static_cast<unsigned>(bodies[i].size()))
            != ZIP_OK)
            ok = false;
        if (::zipCloseFileInZip(zf) != ZIP_OK)
            ok = false;
    }
    if (::zipClose(zf, nullptr) != ZIP_OK)
        ok = false;
    return ok;
}

bool write_bk2(const std::string& path, std::string_view platform) {
    return write_bk2_text(
        path,
        "MovieVersion BizHawk v2.0.0\nPlatform " + std::string(platform) + "\nSHA1 ABC\n",
        "[Input]\nLogKey:#Reset|\n|..|\n[/Input]\n"
    );
}

std::string upper_hex(const cores::DigestValue& d) {
    static constexpr char k[] = "0123456789ABCDEF";
    std::string out;
    for (const std::uint8_t c : d.view()) {
        out += k[c >> 4];
        out += k[c & 15];
    }
    return out;
}

void test_nes_bk2_check_header_forms() {
    Tmp t("/tasty_tasty_nesbk2_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    std::string body(64, '\0');
    for (std::size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<char>(i * 7u + 3u);
    const std::string ines1 = std::string("NES\x1a\x08\x00\x21", 7) + std::string(9, '\0');
    const std::string nes2 = std::string(
        "NES\x1a\x08\x00\x21\x08\x20\x00\x00\x07\x00\x00\x00\x01",
        16
    );
    cores::RomDigest h{cores::DigestKind::Sha1};
    const std::string good = ines1 + body;
    h.update(std::span(reinterpret_cast<const std::uint8_t*>(good.data()), good.size()));
    const std::string log = "[Input]\nLogKey:#Power|Reset|#P1 Up|P1 Down|P1 Left|P1 Right|P1 "
                            "Start|P1 Select|P1 B|P1 A|\n|..|........|\n[/Input]\n";
    const std::string head = "MovieVersion BizHawk v2.0\nPlatform NES\nSHA1 "
        + upper_hex(h.finish())
        + "\n";
    CHECK(write_bk2_text(t.p("nes.bk2"), head, log));
    CHECK(t.put("r.nes", nes2 + body));
    CHECK(fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "nes.bk2", .rom = "r.nes"}).ok());
    CHECK(t.put("r.nes", good));
    CHECK(fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "nes.bk2", .rom = "r.nes"}).ok());
    CHECK(t.put("r.nes", nes2 + body + "x"));
    CHECK(
        fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "nes.bk2", .rom = "r.nes"})
        == fw::tasty::Exit::Code::Mismatch
    );

    cores::RomDigest hb{cores::DigestKind::Sha1};
    hb.update(std::span(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()));
    (void)::unlink(t.p("nes.bk2").c_str());
    CHECK(write_bk2_text(
        t.p("nes.bk2"),
        "MovieVersion BizHawk v2.0\nPlatform NES\nSHA1 " + upper_hex(hb.finish()) + "\n",
        log
    ));
    CHECK(t.put("r.nes", nes2 + body));
    CHECK(fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "nes.bk2", .rom = "r.nes"}).ok());
    (void)::unlink(t.p("nes.bk2").c_str());
    CHECK(write_bk2_text(t.p("nes.bk2"), head, log));
    (void)::unlink(t.p("nes.bk2").c_str());
    CHECK(write_bk2_text(t.p("nes.bk2"), head + "StartsFromSavestate True\n", log));
    {
        StderrTo cap;
        CHECK(
            fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "nes.bk2", .rom = "r.nes"})
            == fw::tasty::Exit::Code::Refused
        );
        CHECK_EQ_STR(
            cap.text(),
            "tasty: this movie starts from a savestate or saved game, not from power-on\n"
        );
    }
    (void)::unlink(t.p("nes.bk2").c_str());
    (void)::unlink(t.p("r.nes").c_str());
}

void test_bk2_sniff() {
    Tmp t("/tasty_tasty_bk2_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    CHECK(write_bk2(t.p("snes.bk2"), "SNES"));
    CHECK(write_bk2(t.p("gen.bk2"), "GEN"));
    CHECK(write_bk2(t.p("psx.bk2"), "PSX"));
    CHECK(write_bk2(t.p("nes.bk2"), "NES"));
    CHECK(write_bk2(t.p("n64.bk2"), "N64"));
    const auto snes = cores::movie_system_for(*vfs, "snes.bk2");
    const auto gen = cores::movie_system_for(*vfs, "gen.bk2");
    const auto psx = cores::movie_system_for(*vfs, "psx.bk2");
    const auto nes = cores::movie_system_for(*vfs, "nes.bk2");
    const auto n64 = cores::movie_system_for(*vfs, "n64.bk2");
    CHECK(snes && snes->conf_str_name == "SNES");
    CHECK(gen && gen->conf_str_name == "MegaDrive");
    CHECK(psx && psx->conf_str_name == "PSX");
    CHECK(nes && nes->conf_str_name == "NES");
    CHECK(!n64);
    const auto n64p = cores::movie_bk2_platform(*vfs, "n64.bk2");
    CHECK(n64p.has_value() && *n64p == "N64");

    CHECK(t.put("junk.cue", "not a sheet\n"));
    std::string err;
    std::optional<fw::TastyRefusal> why;
    {
        StderrTo cap;
        auto ok = fw::TastyPreflight{*vfs}.rom_readable(
            fw::TastyFiles{.movie = "psx.bk2", .rom = "junk.cue"}
        );
        CHECK(!ok.has_value());
        if (!ok)
            why = fw::TastyRefusal::of(ok.error());
        err = cap.text();
    }
    CHECK(why && why->why() == fw::TastyRefusal::RomUnhashable);
    CHECK(
        err
        == "tasty: cannot hash disc image junk.cue; use a Redump .cue/.bin or a chdman .chd of "
           "one\n"
    );

    CHECK(t.put("junk.chd", std::string(4096, 'x')));
    {
        StderrTo cap;
        auto ok = fw::TastyPreflight{*vfs}.rom_readable(
            fw::TastyFiles{.movie = "psx.bk2", .rom = "junk.chd"}
        );
        CHECK(!ok.has_value());
        if (!ok)
            why = fw::TastyRefusal::of(ok.error());
        err = cap.text();
    }
    CHECK(why && why->why() == fw::TastyRefusal::RomUnhashable);
    CHECK(
        err
        == "tasty: cannot hash disc image junk.chd; use a Redump .cue/.bin or a chdman .chd of "
           "one\n"
    );
}

void test_bk2_unknown_platform_refused() {
    Tmp t("/tasty_tasty_bk2u_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    CHECK(write_bk2(t.p("n64.bk2"), "N64"));
    CHECK(t.put("r.nes", "xxxx"));
    const char*
        want = "tasty: this .bk2 is for N64; tasty plays NES, SNES, Genesis and PSX movies\n";
    {
        StderrTo cap;
        CHECK(
            fw::tasty_check_movie(*vfs, fw::TastyFiles{.movie = "n64.bk2", .rom = "r.nes"})
            == fw::tasty::Exit::Code::Refused
        );
        CHECK_EQ_STR(cap.text(), want);
    }
    {
        StderrTo cap;
        const auto ok = fw::TastyPreflight{*vfs}.rom_readable(
            fw::TastyFiles{.movie = "n64.bk2", .rom = "r.nes"}
        );
        CHECK(!ok.has_value());
        const auto why = ok ? std::nullopt : fw::TastyRefusal::of(ok.error());
        CHECK(why && why->why() == fw::TastyRefusal::RomUnplayable);
        CHECK_EQ_STR(cap.text(), want);
    }
    (void)::unlink(t.p("n64.bk2").c_str());
    (void)::unlink(t.p("r.nes").c_str());
}

void test_cli_run_flags() {
    const char* const av[] = {
        "tasty",
        "play",
        "m.fm2",
        "--rom",
        "r.nes",
        "--stop-at",
        "3600",
        "--linger",
        "0",
        "--strict"
    };
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a)
        return;
    CHECK(a->stop_at == std::optional<app::MovieFrame>{app::MovieFrame{3600}});
    CHECK(a->linger == std::chrono::seconds{0});
    CHECK(a->strict);
    const char* const dflt[] = {"tasty", "play", "m.fm2", "--rom", "r.nes"};
    const auto d = fw::parse_tasty_args(std::span<const char* const>(dflt));
    CHECK(d.has_value() && !d->stop_at && !d->strict && d->linger == std::chrono::seconds{30});
    CHECK(d.has_value() && !d->phase_us);
    const char* const old[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--return-after", "5"};
    const auto o = fw::parse_tasty_args(std::span<const char* const>(old));
    CHECK(o.has_value() && o->linger == std::chrono::seconds{5});
    const char* const zero[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--stop-at", "0"};
    CHECK(!fw::parse_tasty_args(std::span<const char* const>(zero)).has_value());
    const char* const ph[] = {"tasty", "play", "m.fm2", "--rom", "r.nes", "--phase", "4000"};
    const auto p = fw::parse_tasty_args(std::span<const char* const>(ph));
    CHECK(
        p.has_value()
        && p->phase_us == std::optional<std::chrono::microseconds>{std::chrono::microseconds{4000}}
    );
    const char* const miss[] = {"tasty", "play", "m.fm2", "--phase"};
    const auto m = fw::parse_tasty_args(std::span<const char* const>(miss));
    CHECK(!m.has_value());
    if (!m) {
        CHECK_EQ(m.error().detail, fw::TastyRefusal{fw::TastyRefusal::BadPhase}.detail());
        CHECK(
            std::string_view(fw::TastyRefusal::sentence_for(m.error()))
            == "--phase needs a whole number of microseconds"
        );
    }
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
        x.args.stop_at = app::MovieFrame{3600};
        x.args.phase_us = std::chrono::microseconds{4000};
        x.args.strict = strict;
        x.s.emplace(x.w, x.args, x.play_run);
        to_run(x);
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(x.replay.last.stop_at == std::optional<app::MovieFrame>{app::MovieFrame{3600}});
        CHECK(
            x.replay.last.phase
            == std::optional<std::chrono::microseconds>{std::chrono::microseconds{4000}}
        );
        CHECK(x.replay.last.set_settings == !strict);
    }
}

void test_session_direct_video_force() {
    xthread::Telemetry<std::uint8_t, SeatTag::Main> cell{};
    {
        cell.publish(std::uint8_t{1});
        StderrTo err;
        Sess x{nullptr, nullptr, {.direct_video_ini = &cell}};
        x.args.strict = true;
        x.s.emplace(x.w, x.args, x.play_run);
        to_run(x);
        const std::string text = err.text();
        CHECK_EQ(x.replay.plays, 0u);
        CHECK(
            text.find(
                "tasty: replay refused (--strict): direct_video is not off; without "
                "--strict tasty sets it for this run\n"
            )
            != std::string::npos
        );
        CHECK(x.exit_code() == 1);
    }
    {
        cell.publish(std::uint8_t{1});
        StderrTo err;
        Sess x{nullptr, nullptr, {.direct_video_ini = &cell}};
        x.s.emplace(x.w, x.args, x.play_run);
        to_run(x);
        x.tick();
        const std::string text = err.text();
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(
            text.find("tasty: direct_video: using 0 for this run (your setting: 1)\n")
            != std::string::npos
        );
        CHECK(x.exit_code() == 0);
    }
    {
        cell.publish(std::uint8_t{0});
        StderrTo err;
        Sess x{nullptr, nullptr, {.direct_video_ini = &cell}};
        x.s.emplace(x.w, x.args, x.play_run);
        to_run(x);
        x.tick();
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(err.text().find("direct_video") == std::string::npos);
    }
    {
        PumpRig pump;
        CHECK(pump.pump.has_value());
        if (!pump.pump)
            return;
        const char* ini = "[MiSTer]\ndirect_video=1\nvideo_mode=8\n";
        CHECK(pump.root.put("MiSTer.ini", ini));
        pump.pump->arm_replay_ini(false);
        cell.publish(std::uint8_t{0});
        Sess x{nullptr, nullptr, {.direct_video_ini = &cell}};
        x.w.video = infra::OptRef{*pump.pump};
        x.s.emplace(x.w, x.args, x.play_run);
        x.tick();
        publish_id(x.ident, "NES");
        x.tick();
        x.tick();
        CHECK_EQ(x.replay.plays, 0u);
        fw::TastySink sink{*pump.pump, {}};
        sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
        (void)pump.tick();
        CHECK_EQ(pump.pump->direct_video_ini(), 1u);
        CHECK(!pump.pump->direct_video());
        StderrTo err;
        x.tick();
        x.tick();
        x.tick();
        const std::string text = err.text();
        CHECK_EQ(x.replay.plays, 1u);
        CHECK(
            text.find("tasty: direct_video: using 0 for this run (your setting: 1)\n")
            != std::string::npos
        );
    }
}

void test_strict_rec_start_waits_for_the_ini() {
    PumpRig pump;
    CHECK(pump.pump.has_value());
    if (!pump.pump)
        return;
    CHECK(pump.root.put("MiSTer.ini", "[MiSTer]\ndirect_video=1\nvideo_mode=8\n"));
    pump.pump->arm_replay_ini(true);
    xthread::Telemetry<std::uint8_t, SeatTag::Main> cell{};
    Sess x{nullptr, nullptr, {.direct_video_ini = &cell}};
    x.w.video = infra::OptRef{*pump.pump};
    x.args.no_splash = true;
    x.args.strict = true;
    x.s.emplace(x.w, x.args, x.rec_run);
    {
        StderrTo err;
        for (int i = 0; i < 4; ++i)
            x.tick();
        CHECK(err.text().find("direct_video") == std::string::npos);
        CHECK(x.exit_code() != 1);
    }
    {
        const SeatScope as_main{SeatTag::Main};
        cell.publish(std::uint8_t{0});
    }
    fw::TastySink sink{*pump.pump, {}};
    sink.deliver(infra::make<app::Event>(app::Event::CoreLoaded{}, app::Event::Head{}));
    (void)pump.tick();
    CHECK_EQ(pump.pump->direct_video_ini(), 1u);
    StderrTo err;
    x.tick();
    x.tick();
    CHECK(
        err.text().find("replay refused (--strict): direct_video is not off") != std::string::npos
    );
    CHECK(x.exit_code() == 1);
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
    CHECK(
        err
        == "tasty: System Type: using NTSC for this run (your setting: PAL)\n"
           "tasty: RAM Clear: using $00 for this run (your setting: No)\n"
    );
    CHECK(x.exit_code() == 0);
}

void test_refusal_sentences() {
    struct Case {
        app::PlayRefusal::Why why{};
        std::optional<cores::MovieRefusal> movie{};
        std::string_view setting{};
        std::string_view path{};
        bool strict{};
        bool offered{};
        const char* want{};
    };

    const Case cases[] = {
        {.why = app::PlayRefusal::Why::Setting,
         .setting = "RAM Clear",
         .strict = true,
         .offered = true,
         .want = "tasty: replay refused (--strict): the core's RAM Clear is not the movie's; "
                 "without --strict tasty sets it for this run\n"},
        {.why = app::PlayRefusal::Why::Setting,
         .setting = "Region",
         .strict = true,
         .offered = false,
         .want = "tasty: replay refused: this movie needs a Region this core does not offer\n"},
        {.why = app::PlayRefusal::Why::Setting,
         .setting = "Region",
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: this movie needs a Region this core does not offer\n"},
        {.why = app::PlayRefusal::Why::Setting,
         .setting = "RAM Clear",
         .strict = false,
         .offered = true,
         .want = "tasty: replay refused: tasty could not set the core's RAM Clear for this run\n"},
        {.why = app::PlayRefusal::Why::Firmware,
         .path = "/media/fat/games/PSX/cd_bios.rom",
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: the movie was recorded with another BIOS; it needs its "
                 "own at /media/fat/games/PSX/cd_bios.rom\n"},
        {.why = app::PlayRefusal::Why::RomChd,
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: this build of tasty cannot read .chd disc images\n"},
        {.why = app::PlayRefusal::Why::Movie,
         .movie = cores::MovieRefusal::Savestate,
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: m.fm2 starts from a savestate or saved game, not from "
                 "power-on\n"},
        {.why = app::PlayRefusal::Why::Movie,
         .movie = cores::MovieRefusal::PortType,
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: this movie uses a controller other than the standard "
                 "pad, which tasty does not replay\n"},
        {.why = app::PlayRefusal::Why::Movie,
         .movie = cores::MovieRefusal::Setting,
         .strict = false,
         .offered = false,
         .want = "tasty: replay refused: this movie was recorded with an emulator setting, such "
                 "as its region, that this core cannot match\n"},
    };
    for (const Case& c : cases) {
        Sess x;
        x.args.strict = c.strict;
        x.s.emplace(x.w, x.args, x.play_run);
        to_run(x);
        std::string err;
        {
            StderrTo cap;
            x.replay.ref = fw::ReplayRefusal{
                .why = c.why,
                .movie = c.movie,
                .setting = c.setting,
                .setting_offered = c.offered,
                .path = c.path
            };
            x.tick();
            err = cap.text();
        }
        CHECK(err == c.want);
        CHECK(err.find("menu") == std::string::npos);
        CHECK(x.exit_code() == 1);
    }
}

void test_movie_refusals_have_sentences() {
    using R = cores::MovieRefusal;
    for (std::uint32_t r = 1; r < static_cast<std::uint32_t>(R::kCount); ++r) {
        const R why = static_cast<R>(r);
        CHECK(fw::tasty_movie_refusal_sentence(why) != nullptr);
    }
    using W = app::PlayRefusal::Why;
    for (std::uint32_t w = 1; w < static_cast<std::uint32_t>(W::kCount); ++w) {
        char buf[1400];
        CHECK(
            fw::tasty_refusal_sentence(fw::ReplayRefusal{.why = static_cast<W>(w)}, {}, false, buf)
            != nullptr
        );
    }
    char buf[1400];
    CHECK(
        fw::tasty_refusal_sentence(fw::ReplayRefusal{.why = W::kCount}, {}, false, buf) == nullptr
    );
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
    while (x.tx.push(proto::LinkOp::SetCoreOption{}))
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
        if (vis && vis->show == proto::LinkOp::OsdShow::Off)
            hidden = true;
    }
    CHECK(hidden);
}

void publish_running(app::ReplayStatusCell& cell, std::int32_t frame) {
    const SeatScope rt{SeatTag::RT};
    app::ReplayStatus s{};
    s.level = app::ReplayLevel::Running;
    s.movie_frame = app::MovieFrame{frame};
    cell.publish(s);
}

void test_recorded_replay_rereads_the_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump)
        return;
    Tmp t("/tasty_tasty_recgeo_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    fw::TastySink sink{*r.pump, {}};
    Sess x;
    x.w.video = infra::OptRef{*r.pump};
    app::PathText cap{};
    CHECK(cap.assign(t.p("cap")));
    x.args.record = cap;

    (void)x.args.movie.assign("m.gmv");
    x.s.emplace(x.w, x.args, x.play_run);
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
            r.pump->wire().on_rt_round(r.link.view, r.t);
            r.t += 600'000'000;
        }
    };
    passes(2);
    CHECK(!r.tick().rate.known());
    r.link.vtime = 1'668'814;
    passes(2);

    CHECK(!r.tick().rate.known());
    publish_running(x.play, 59);
    x.tick();
    (void)r.tick();
    passes(2);
    CHECK(!r.tick().rate.known());
    publish_running(x.play, 60);
    x.tick();
    (void)r.tick();
    passes(2);
    CHECK(r.tick().rate == app::FrameTicks::plausible(1'668'814));
}

void test_unrecorded_replay_leaves_the_geometry() {
    PumpRig r;
    CHECK(r.pump.has_value());
    if (!r.pump)
        return;
    fw::TastySink sink{*r.pump, {}};
    Sess x;
    x.w.video = infra::OptRef{*r.pump};

    (void)x.args.movie.assign("m.gmv");
    x.s.emplace(x.w, x.args, x.play_run);
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
            r.pump->wire().on_rt_round(r.link.view, r.t);
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
    CHECK(!r.tick().rate.known());
}

bool mkdir_p(const std::string& path) {
    std::string cur;
    for (std::size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] != '/' && i + 1 != path.size())
            continue;
        if (cur.size() <= 1)
            continue;
        if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

bool put_file(const std::string& path, std::string_view body) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    std::fclose(f);
    return ok;
}

bool put_nes_banks(const std::string& path, unsigned prg, unsigned chr) {
    const std::uint64_t span = std::uint64_t{prg} * 16384ull + std::uint64_t{chr} * 8192ull;
    const int fd = ::open(path.c_str(), O_CLOEXEC | O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
        return false;
    unsigned char head[16]{};
    head[0] = 'N';
    head[1] = 'E';
    head[2] = 'S';
    head[3] = 0x1A;
    head[4] = static_cast<unsigned char>(prg);
    head[5] = static_cast<unsigned char>(chr);
    const ssize_t n = ::write(fd, head, sizeof head);
    const int tr = ::ftruncate(fd, static_cast<off_t>(16 + span));
    ::close(fd);
    return n == static_cast<ssize_t>(sizeof head) && tr == 0;
}

struct Gone {
    std::vector<std::string> files;
    std::vector<std::string> dirs;

    ~Gone() {
        for (const std::string& f : files)
            (void)::unlink(f.c_str());
        for (auto it = dirs.rbegin(); it != dirs.rend(); ++it)
            (void)::rmdir(it->c_str());
    }
};

std::string nes_checksum_line(std::string_view body) {
    cores::RomDigest dig{cores::DigestKind::Md5};
    dig.update(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(body.data()),
            body.size()
        )
    );
    const auto d = dig.finish();
    static constexpr char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "romChecksum base64:";
    for (std::size_t i = 0; i < 16; i += 3) {
        const std::uint32_t n = (std::uint32_t{d.bytes[i]} << 16)
            | (i + 1 < 16 ? std::uint32_t{d.bytes[i + 1]} << 8 : 0u)
            | (i + 2 < 16 ? std::uint32_t{d.bytes[i + 2]} : 0u);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < 16 ? k[(n >> 6) & 63] : '=';
        out += i + 2 < 16 ? k[n & 63] : '=';
    }
    return out;
}

std::string nes_rom_bytes() {
    std::string rom(16 + 16384 + 8192, '\x5A');
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;

    for (int i = 6; i < 16; ++i)
        rom[static_cast<std::size_t>(i)] = 0;
    return rom;
}

std::string nes_movie(std::string_view prg_chr) {
    return "version 3\n"
        + nes_checksum_line(prg_chr)
        + "\nrerecordCount 5\nport0 1\nport1 0\nport2 0\n"
          "|0|........|||\n|0|........|||\n|0|.......A|||\n";
}

void test_loop_with_record_is_refused() {
    Tmp t("/tasty_tasty_loop_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    fw::TastyArgs a{};
    CHECK(a.movie.assign("m.fm2"));
    app::PathText out{};
    CHECK(out.assign("out.avi"));
    a.record = out;
    a.loop = true;
    StderrTo err;
    CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
    const std::string text = err.text();
    CHECK(text.find("--loop") != std::string::npos);
    CHECK(text.find("--record") != std::string::npos);
}

void test_rerecords_need_a_whole_decimal() {
    CHECK(!cores::rerecord_count("rerecordCount", "12x"));
    CHECK(!cores::rerecord_count("rerecordCount", "-1"));
    CHECK(!cores::rerecord_count("rerecordCount", "99999999999"));
    CHECK(!cores::rerecord_count("comment", "5"));
    CHECK(
        cores::rerecord_count("rerecords", "4294967295") == cores::MovieRerecordCount{4294967295u}
    );
}

void test_ram_fill_is_the_codecs() {
    const auto nes = cores::movie_codec_for("NES", "m.fm2");
    const auto snes = cores::movie_codec_for("SNES", "m.lsmv");
    CHECK(nes.has_value() && snes.has_value());
    if (!nes || !snes)
        return;
    using Fill = cores::IReplayCore::RamFill;
    CHECK(!snes->core().ram_fill_need(Fill::Zero).has_value());
    CHECK(!snes->core().setting_needs_for({}, Fill::Zero).has_value());
    const auto ff = nes->core().ram_fill_need(Fill::Ff);
    CHECK(ff.has_value() && ff->preferred.v == 2 && ff->allowed == proto::ValueSet{1u << 2});
    const auto needs = nes->core().setting_needs_for({}, Fill::Random);
    CHECK(needs.has_value());
    if (!needs)
        return;
    unsigned rows = 0;
    for (const auto& n : needs->view()) {
        if (n.name != ff->name)
            continue;
        ++rows;
        CHECK(n.preferred.v == 3 && n.allowed == proto::ValueSet{1u << 3});
    }
    CHECK_EQ(rows, 1u);
}

void test_prepare_play_lead_rom_and_ram() {
    Tmp t("/tasty_tasty_prep_");
    CHECK(!t.root.empty());
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    const std::string rom = nes_rom_bytes();
    const std::string movie = nes_movie(std::string_view(rom).substr(16));
    CHECK(t.put("m.fm2", movie));
    fw::TastyArgs a{};
    CHECK(a.movie.assign("m.fm2"));
    a.lead = app::FrameCount{1000};
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        const std::string text = err.text();
        CHECK(text.find("outside") != std::string::npos);
        CHECK(text.find("--lead") != std::string::npos);
    }
    a.lead = app::FrameCount{-9};
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        CHECK(err.text().find("outside") != std::string::npos);
    }
    CHECK(t.put("r.nes", rom));
    a.lead = app::FrameCount{-3};
    CHECK(a.rom.assign(t.p("r.nes")));
    CHECK(fw::TastyPreflight{*vfs}.play_rom(a).has_value());
    a.phase_us = std::chrono::microseconds{0};
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        CHECK(err.text().find("leave 2000 us before the next frame") != std::string::npos);
    }
    a.phase_us = std::chrono::microseconds{20'000};
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        CHECK(err.text().find("leave 2000 us before the next frame") != std::string::npos);
    }

    for (const std::uint32_t bad : {999u, 14'700u, 15'000u}) {
        a.phase_us = std::chrono::microseconds{bad};
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        CHECK(err.text().find("leave 2000 us before the next frame") != std::string::npos);
    }
    for (const std::uint32_t good : {1000u, 4000u, 14'600u}) {
        a.phase_us = std::chrono::microseconds{good};
        CHECK(fw::TastyPreflight{*vfs}.play_rom(a).has_value());
    }
    a.phase_us.reset();
    a.lead.reset();
    CHECK(fw::TastyPreflight{*vfs}.play_rom(a).has_value());
    a.rom = {};
    a.ram_fill = cores::IReplayCore::RamFill::Zero;
    CHECK(a.movie.assign("x.lsmv"));
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        const std::string text = err.text();
        CHECK(text.find("NES") != std::string::npos);
        CHECK(text.find("RAM Clear") != std::string::npos);
    }
    a.ram_fill.reset();
    CHECK(a.movie.assign("m.fm2"));
    const std::string nes_dir = t.root + "/media/fat/games/NES";
    const std::string sub = nes_dir + "/usa";
    CHECK(mkdir_p(sub));
    CHECK(put_file(nes_dir + "/a.nes", rom));
    CHECK(put_file(sub + "/smb.nes", rom));
    const auto first = fw::TastyPreflight{*vfs}.play_rom(a);
    CHECK(first.has_value());
    CHECK(first->view().find("/NES/a.nes") != std::string_view::npos);
    a.rom = {};
    (void)::unlink((nes_dir + "/a.nes").c_str());
    const auto second = fw::TastyPreflight{*vfs}.play_rom(a);
    CHECK(second.has_value());
    CHECK(second->view().find("/usa/smb.nes") != std::string_view::npos);
    a.rom = {};
    (void)::unlink((sub + "/smb.nes").c_str());
    std::string other = rom;
    other[100] = '\x11';
    CHECK(put_file(nes_dir + "/other.nes", other));
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
        const std::string text = err.text();
        CHECK(text.find("/media/fat/games/NES") != std::string::npos);
        CHECK(text.find("--rom") != std::string::npos);
        CHECK(text.find("stops there") == std::string::npos);
    }
    std::string gmv(64, '\0');
    std::memcpy(gmv.data(), "Gens Movie TEST", 15);
    gmv[0x0F] = 'A';
    gmv[0x10] = 7;
    gmv[0x14] = '3';
    gmv[0x15] = '3';
    CHECK(t.put("m.gmv", gmv));
    fw::TastyArgs g{};
    CHECK(g.movie.assign("m.gmv"));
    {
        StderrTo err;
        CHECK(!fw::TastyPreflight{*vfs}.play_rom(g).has_value());
        CHECK(err.text().find("no ROM checksum") != std::string::npos);
    }
    (void)::unlink((nes_dir + "/other.nes").c_str());
    (void)::unlink((sub + "/smb.nes").c_str());
    (void)::rmdir(sub.c_str());
    (void)::rmdir(nes_dir.c_str());
    (void)::rmdir((t.root + "/media/fat/games").c_str());
    (void)::rmdir((t.root + "/media/fat").c_str());
    (void)::rmdir((t.root + "/media").c_str());
    (void)::unlink(t.p("m.fm2").c_str());
    (void)::unlink(t.p("r.nes").c_str());
    (void)::unlink(t.p("m.gmv").c_str());
}

void test_tasty_refusal_round_trips() {
    constexpr std::uint8_t kFirst = 1;
    constexpr std::uint8_t kLast = std::to_underlying(fw::TastyRefusal::kCount) - 1;
    for (std::uint8_t i = kFirst; i <= kLast; ++i) {
        const auto why = static_cast<fw::TastyRefusal::Why>(i);
        const fw::TastyRefusal r{why};
        const Error e = r.error(infra::kNoSite);
        const auto back = fw::TastyRefusal::of(e);
        CHECK(back.has_value() && back->why() == why);
        CHECK_EQ(e.detail, r.detail());
        CHECK_EQ(r.detail(), 0x74610000u | i);
    }

    CHECK_EQ(fw::TastyRefusal{fw::TastyRefusal::NoVerb}.detail(), 0x74610001u);
    CHECK_EQ(fw::TastyRefusal{fw::TastyRefusal::SaveNotPlay}.detail(), 0x7461001du);
    CHECK_EQ(fw::TastyRefusal{fw::TastyRefusal::LoopWithRecord}.detail(), 0x7461001eu);
    CHECK_EQ(fw::TastyRefusal{fw::TastyRefusal::RecordTooLong}.detail(), 0x74610031u);

    CHECK(!fw::TastyRefusal::of(
        Error{Errc::bad_format, infra::kNoSite, fw::TastyRefusal{fw::TastyRefusal::kCount}.detail()}
    ));
    CHECK(!fw::TastyRefusal::of(Error{Errc::bad_format, infra::kNoSite, 7}));
    CHECK(
        !fw::TastyRefusal::of(cores::movie_refusal(cores::MovieRefusal::BadLine, infra::kNoSite))
    );
    CHECK(!fw::TastyRefusal::of(
        Error{Errc::os, infra::kNoSite, fw::TastyRefusal{fw::TastyRefusal::NoVerb}.detail()}
    ));

    for (std::uint8_t i = kFirst; i <= kLast; ++i) {
        const auto why = static_cast<fw::TastyRefusal::Why>(i);
        const bool figure = why == fw::TastyRefusal::NoCodec
            || why == fw::TastyRefusal::MovieUnreadable
            || why == fw::TastyRefusal::LeadOutOfRange
            || why == fw::TastyRefusal::RomNotFound
            || why == fw::TastyRefusal::RomSearchStopped
            || why == fw::TastyRefusal::RomUnplayable
            || why == fw::TastyRefusal::RomUnhashable;
        CHECK_EQ(fw::TastyRefusal{why}.sentence() == nullptr, figure);
    }
    CHECK(
        std::string_view(fw::TastyRefusal::sentence_for(Error{Errc::bad_format, infra::kNoSite, 0}))
        == "bad arguments (try tasty help)"
    );
}

void test_prepare_play_names_its_refusal() {
    Tmp t("/tasty_tasty_t9_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;

    auto refused = [](auto&& call, fw::TastyRefusal::Why why, std::string_view want) {
        StderrTo err;
        const auto r = call();
        const std::string text = err.text();
        CHECK(!r.has_value());
        const auto got = r ? std::nullopt : fw::TastyRefusal::of(r.error());
        CHECK(got.has_value() && got->why() == why);
        CHECK(text.find('\n') == text.size() - 1);
        if (!want.empty())
            CHECK(std::string_view{text} == want);
        else if (got && got->sentence() != nullptr)
            CHECK(text == "tasty: " + std::string{got->sentence()} + "\n");
    };
    fw::TastyArgs a{};
    CHECK(a.movie.assign("m.fm2"));
    a.loop = true;
    CHECK(a.record.emplace().assign("out.avi"));
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::LoopWithRecord,
        "tasty: --loop and --record do not mix; drop --loop to record one pass\n"
    );
    a.loop = false;
    a.record.reset();
    CHECK(a.movie.assign("x.nope"));
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::NoCodec,
        "tasty: no codec for x.nope\n"
    );

    CHECK(a.movie.assign("x.lsmv"));
    a.ram_fill = cores::IReplayCore::RamFill::Zero;
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::RamInitUnsupported,
        "tasty: this movie's core has no RAM-init setting; --ram-init works only where it has "
        "one (NES: RAM Clear)\n"
    );
    a.ram_fill.reset();
    CHECK(t.put("bad.fm2", "not a movie\n"));
    CHECK(a.movie.assign("bad.fm2"));
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::MovieUnreadable,
        "tasty: cannot read movie bad.fm2\n"
    );

    const std::string rom = nes_rom_bytes();
    CHECK(t.put("m.fm2", nes_movie(std::string_view(rom).substr(16))));
    CHECK(a.movie.assign("m.fm2"));
    a.lead = app::FrameCount{1000};
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::LeadOutOfRange,
        ""
    );
    a.lead.reset();
    a.phase_us = std::chrono::microseconds{0};
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::PhaseOutOfRange,
        "tasty: --phase must be at least 1000 us and leave 2000 us before the next frame\n"
    );
    a.phase_us.reset();
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(a); },
        fw::TastyRefusal::RomNotFound,
        "tasty: no ROM in /media/fat/games/NES matches this movie; pass --rom\n"
    );

    std::string gmv(64, '\0');
    std::memcpy(gmv.data(), "Gens Movie TEST", 15);
    gmv[0x0F] = 'A';
    gmv[0x10] = 7;
    gmv[0x14] = '3';
    gmv[0x15] = '3';
    CHECK(t.put("m.gmv", gmv));
    fw::TastyArgs g{};
    CHECK(g.movie.assign("m.gmv"));
    refused(
        [&] { return fw::TastyPreflight{*vfs}.play_rom(g); },
        fw::TastyRefusal::RomNeedsChecksum,
        "tasty: this movie names no ROM checksum; pass --rom\n"
    );

    auto record_refused = [&](const std::string& path, fw::TastyRefusal::Why why) {
        app::PathText rec{};
        CHECK(rec.assign(path));
        refused(
            [&] { return fw::tasty_prepare_record(rec, "m.fm2"); },
            why,
            "tasty: " + std::string{fw::TastyRefusal{why}.sentence()} + " (" + path + ")\n"
        );
    };
    refused(
        [&] { return fw::tasty_prepare_record(app::PathText{}, "m.fm2"); },
        fw::TastyRefusal::RecordNoTarget,
        "tasty: that record path names no directory or file ()\n"
    );
    CHECK(::mkdir(t.p("cap.avi").c_str(), 0755) == 0);
    record_refused(t.p("cap.avi"), fw::TastyRefusal::RecordIsDirectory);
    (void)::rmdir(t.p("cap.avi").c_str());
    record_refused(t.p(".avi"), fw::TastyRefusal::RecordNoStem);
    CHECK(t.put("capfile", "x"));
    record_refused(t.p("capfile"), fw::TastyRefusal::RecordIsFile);
    (void)::unlink(t.p("capfile").c_str());

    const std::string overlong = t.root + "/" + std::string(300, 'a');
    record_refused(overlong, fw::TastyRefusal::RecordDirFailed);
    record_refused(overlong + "/x.avi", fw::TastyRefusal::RecordParentFailed);

    std::string deep = t.root;
    std::string levels[4];
    for (std::string& level : levels) {
        level.assign(220, 'b');
        deep += "/" + level;
    }
    CHECK(mkdir_p(deep));
    app::PathText deeprec{};
    CHECK(deeprec.assign(deep));
    const std::string long_movie = std::string(200, 'c') + ".fm2";
    refused(
        [&] { return fw::tasty_prepare_record(deeprec, long_movie); },
        fw::TastyRefusal::RecordTooLong,
        "tasty: that record path is too long (" + deep + ")\n"
    );
    for (std::size_t i = 4; i-- > 0;) {
        deep.erase(deep.size() - levels[i].size() - 1);
        (void)::rmdir(deep.c_str());
    }
}

void test_cli_record_empty() {
    const char* const av[] = {"tasty", "play", "m.fm2", "--record", ""};
    const auto a = fw::parse_tasty_args(std::span<const char* const>(av));
    CHECK(a.has_value());
    if (!a)
        return;
    CHECK(a->record.has_value() && a->record->empty());
    const char* const none[] = {"tasty", "play", "m.fm2"};
    const auto b = fw::parse_tasty_args(std::span<const char* const>(none));
    CHECK(b.has_value() && !b->record.has_value());
    const char* const rec[] = {"tasty", "rec", "start", "--record", ""};
    const auto c = fw::parse_tasty_args(std::span<const char* const>(rec));
    CHECK(c.has_value() && c->verb == fw::TastyVerb::RecStart && c->record && c->record->empty());
    if (!c || !c->record)
        return;
    StderrTo err;
    CHECK(!fw::tasty_prepare_record(*c->record, {}).has_value());
    CHECK(err.text().find("that record path names no directory or file") != std::string::npos);
}

void test_rom_search_stop_sentence_end_to_end() {
    Tmp t("/tasty_tasty_stop_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    auto vfs = svc::Vfs::create_at(t.root);
    CHECK(vfs.has_value());
    if (!vfs)
        return;
    const std::string rom = nes_rom_bytes();
    const std::string movie = nes_movie(std::string_view(rom).substr(16));
    CHECK(t.put("m.fm2", movie));
    const std::string nes_dir = t.root + "/media/fat/games/NES";
    Gone gone;
    gone.dirs.push_back(t.root + "/media");
    gone.dirs.push_back(t.root + "/media/fat");
    gone.dirs.push_back(t.root + "/media/fat/games");
    gone.dirs.push_back(nes_dir);
    CHECK(mkdir_p(nes_dir));

    for (unsigned i = 0; i < 17; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "b%02u.nes", i);
        const std::string file = nes_dir + "/" + name;
        gone.files.push_back(file);
        CHECK(put_nes_banks(file, 255, 1));
    }
    fw::TastyArgs a{};
    CHECK(a.movie.assign("m.fm2"));
    StderrTo err;
    CHECK(!fw::TastyPreflight{*vfs}.play_rom(a).has_value());
    const std::string text = err.text();
    CHECK(
        text.find("no ROM among the first 17 files in /media/fat/games/NES") != std::string::npos
    );
    CHECK(text.find("the search stops there") != std::string::npos);
    CHECK(text.find("--rom") != std::string::npos);
}

struct ChildEnd {
    bool exited = false;
    int code = 0;
    bool signaled = false;
    int sig = 0;
};

template <typename F>
ChildEnd in_child(F fn) {
    const pid_t pid = ::fork();
    CHECK(pid >= 0);
    if (pid < 0)
        return {};
    if (pid == 0) {
        const int rc = fn();
        ::_exit(rc);
    }
    int st = 0;
    for (;;) {
        const pid_t w = ::waitpid(pid, &st, 0);
        if (w == pid)
            break;
        if (w < 0 && errno == EINTR)
            continue;
        return {};
    }
    ChildEnd e;
    if (WIFEXITED(st)) {
        e.exited = true;
        e.code = WEXITSTATUS(st);
    } else if (WIFSIGNALED(st)) {
        e.signaled = true;
        e.sig = WTERMSIG(st);
    }
    return e;
}

using LaunchCount = fw::HomeLane::LaunchCount;

bool self_exe(char* dst, std::size_t n);

struct HomeMarker {
    char exe[4096] = {};
    char arg[4200] = {};
    char path[sizeof "/tmp/tasty_tasty_homeXXXXXX"] = "/tmp/tasty_tasty_homeXXXXXX";
    bool ok = false;

    HomeMarker() {
        const int fd = ::mkstemp(path);
        if (fd < 0)
            return;
        ::close(fd);
        (void)::unlink(path);
        if (!self_exe(exe, sizeof exe))
            return;
        std::snprintf(arg, sizeof arg, "--signal-probe=%s", path);
        ok = true;
    }

    ~HomeMarker() {
        if (path[0] != '\0')
            (void)::unlink(path);
    }

    HomeMarker(const HomeMarker&) = delete;
    HomeMarker& operator=(const HomeMarker&) = delete;

    [[nodiscard]] bool started() const { return ::access(path, F_OK) == 0; }

    [[nodiscard]] bool wait_started(int tries = 250) const {
        for (int i = 0; i < tries && !started(); ++i)
            ::usleep(20 * 1000);
        return started();
    }
};

struct SharedLaunch {
    LaunchCount* p = nullptr;

    SharedLaunch() {
        char path[] = "/tmp/tasty_tasty_sigXXXXXX";
        const int fd = ::mkstemp(path);
        if (fd < 0)
            return;
        (void)::unlink(path);
        if (::ftruncate(fd, static_cast<off_t>(sizeof(LaunchCount))) != 0) {
            ::close(fd);
            return;
        }
        void* m = ::mmap(nullptr, sizeof(LaunchCount), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (m == MAP_FAILED)
            return;
        p = new (m) LaunchCount();
    }

    ~SharedLaunch() {
        if (p == nullptr)
            return;
        p->~LaunchCount();
        (void)::munmap(p, sizeof(LaunchCount));
    }

    SharedLaunch(const SharedLaunch&) = delete;
    SharedLaunch& operator=(const SharedLaunch&) = delete;
};

void test_home_lane_launch_count_shared_with_child() {
    LaunchCount* shared = nullptr;
    void* m = ::mmap(
        nullptr,
        sizeof(LaunchCount),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );
    CHECK(m != MAP_FAILED);
    if (m == MAP_FAILED)
        return;
    shared = new (m) LaunchCount();
    const ChildEnd end = in_child([shared] {
        {
            fw::ReturnHome h{{.probe = true}};
            fw::HomeLane::launch();
        }
        *shared = fw::HomeLane::launches();
        return *shared == LaunchCount::of(1) ? 0 : 1;
    });
    CHECK(end.exited && end.code == 0);
    CHECK(*shared == LaunchCount::of(1));
    shared->~LaunchCount();
    (void)::munmap(m, sizeof(LaunchCount));
}

void test_home_lane_unbind_clears_the_probe() {
    const ChildEnd end = in_child([] {
        fw::ReturnHome h{{.image = {"/nonexistent/tasty_tasty_home", "menu.rbf"}, .probe = true}};
        fw::HomeLane::unbind();
        fw::HomeLane::launch();
        return fw::HomeLane::launches() == LaunchCount{} ? 0 : 1;
    });
    CHECK(end.exited && end.code == 0);
}

void test_owner_sighup_keeps_running() {
    const ChildEnd end = in_child([] {
        fw::tasty_arm_owner_signals();
        Sess x{nullptr, &fw::tasty_owner_stop_flag()};
        to_run(x);
        while (x.load_asked()) {
        }
        const unsigned stops = x.replay.stops;
        ::raise(SIGHUP);
        x.tick();
        if (fw::tasty_owner_stop_flag().load(std::memory_order_seq_cst) != 0)
            return 2;
        if (x.replay.stops != stops)
            return 3;
        if (x.load_asked())
            return 4;
        return 0;
    });
    CHECK(end.exited && end.code == 0);
}

void test_owner_sigterm_ends_and_homes_once() {
    SharedLaunch n;
    CHECK(n.p != nullptr);
    if (n.p == nullptr)
        return;
    const ChildEnd end = in_child([&] {
        {
            fw::ReturnHome home{{.probe = true}};
            fw::HomeLane::owe();
            fw::tasty_arm_owner_signals();
            Sess x{nullptr, &fw::tasty_owner_stop_flag()};
            to_run(x);
            while (x.load_asked()) {
            }
            ::raise(SIGTERM);
            if (fw::HomeLane::launches() != LaunchCount{})
                return 2;
            if (fw::tasty_owner_stop_flag().load(std::memory_order_seq_cst) == 0)
                return 3;
            x.tick();
            if (x.replay.stops < 1)
                return 4;
            if (fw::HomeLane::launches() != LaunchCount{})
                return 5;
        }
        *n.p = fw::HomeLane::launches();
        return 0;
    });
    CHECK(end.exited && end.code == 0);
    CHECK(*n.p == LaunchCount::of(1));
}

void test_owner_closed_stdio_survives() {
    const ChildEnd end = in_child([] {
        fw::tasty_arm_owner_signals();
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0)
            return 2;
        ::close(fds[0]);
        if (::dup2(fds[1], STDOUT_FILENO) < 0 || ::dup2(fds[1], STDERR_FILENO) < 0)
            return 3;
        ::close(fds[1]);
        std::fprintf(stdout, "out\n");
        std::fflush(stdout);
        std::fprintf(stderr, "err\n");
        std::fflush(stderr);
        const char raw[] = "raw\n";
        (void)std::fwrite(raw, 1, sizeof raw - 1, stderr);
        std::fflush(stderr);
        fw::tasty_say("tasty: still here");
        Sess x{nullptr, &fw::tasty_owner_stop_flag()};
        to_run(x);
        x.replay.lines.push_back("kept");
        x.tick();
        if (x.replay.stops != 0)
            return 4;
        return 0;
    });
    CHECK(end.exited && end.code == 0);
}

int ref_line(char* out, std::size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = std::vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return r;
}

template <class... A>
void check_render(
    std::span<char> buf,
    std::string_view want,
    std::format_string<A...> f,
    A&&... a
) {
    const std::string_view got = fw::tasty_render(buf, f, std::forward<A>(a)...);
    CHECK_EQ_STR(std::string{got}, std::string{want});
    CHECK(got.size() < buf.size());
    CHECK(buf[got.size()] == '\0');
}

void test_tasty_render_matches_snprintf() {

    {
        char mine[16]{};
        char ref[16]{};
        const std::string_view text = "abcdefghijklmnopqrstuvwxyz";
        (void)ref_line(ref, sizeof ref, "%s", std::string(text).c_str());
        check_render(mine, std::string_view{ref}, "{}", text);
    }

    {
        char mine[16]{};
        char ref[16]{};
        std::snprintf(ref, sizeof ref, "%d", -42);
        check_render(mine, std::string_view{ref}, "{}", -42);
    }

    {
        char mine[16]{};
        char ref[16]{};
        const std::uint32_t n = 4000000000u;
        std::snprintf(ref, sizeof ref, "%u", n);
        check_render(mine, std::string_view{ref}, "{}", n);
    }

    {
        char mine[16]{};
        char ref[16]{};
        const std::string empty;
        const std::string fits(15, 'a');
        const std::string cuts(16, 'a');
        (void)ref_line(ref, sizeof ref, "%s", empty.c_str());
        check_render(mine, std::string_view{ref}, "{}", std::string_view{empty});
        (void)ref_line(ref, sizeof ref, "%s", fits.c_str());
        check_render(mine, std::string_view{ref}, "{}", std::string_view{fits});
        (void)ref_line(ref, sizeof ref, "%s", cuts.c_str());
        check_render(mine, std::string_view{ref}, "{}", std::string_view{cuts});
    }

    const std::pair<std::string, std::string> lines[] = {
        {"one line", "one line"},
        {"already ended\n", "already ended\n"}
    };
    for (const auto& [text, plain] : lines) {
        std::string got;
        {
            StderrTo cap;
            fw::tasty_sayf("{}", std::string_view{text});
            got = cap.text();
        }
        fw::TastyLineBuf ref;
        (void)ref_line(ref.data(), ref.size(), "%s", plain.c_str());
        std::string old;
        {
            StderrTo cap;
            fw::tasty_say(std::string_view{ref.data()});
            old = cap.text();
        }
        CHECK_EQ_STR(got, old);
        const std::size_t want = plain.back() == '\n' ? plain.size() : plain.size() + 1;
        CHECK_EQ(got.size(), want);
    }
    {
        const std::string big(2000, 'a');
        std::string got;
        {
            StderrTo cap;
            fw::tasty_sayf("{}", std::string_view{big});
            got = cap.text();
        }
        fw::TastyLineBuf ref;
        (void)ref_line(ref.data(), ref.size(), "%s", big.c_str());
        std::string old;
        {
            StderrTo cap;
            fw::tasty_say(std::string_view{ref.data()});
            old = cap.text();
        }
        CHECK_EQ_STR(got, old);
        CHECK_EQ(got.size(), fw::TastyLineBuf{}.size());
        CHECK(got.back() == '\n');
    }

    const ChildEnd end = in_child([] {
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0)
            return 2;
        ::close(fds[0]);
        if (::dup2(fds[1], STDERR_FILENO) < 0)
            return 3;
        ::close(fds[1]);
        fw::tasty_sayf("tasty: {}", std::string_view{"dropped"});
        return 0;
    });
    CHECK(end.exited && end.code == 0);
}

void test_converted_user_lines_are_unchanged() {

    const char* sentence = fw::TastyRefusal{fw::TastyRefusal::BadLead}.sentence();
    fw::TastyLineBuf ref;
    std::snprintf(ref.data(), ref.size(), "tasty: %s", sentence);
    std::string got;
    {
        StderrTo cap;
        fw::TastyRefusal{fw::TastyRefusal::BadLead}.say();
        got = cap.text();
    }
    std::string old;
    {
        StderrTo cap;
        fw::tasty_say(std::string_view{ref.data()});
        old = cap.text();
    }
    CHECK_EQ_STR(got, old);
    CHECK_EQ_STR(got, "tasty: --lead needs a whole number of frames\n");

    const std::string setting(1500, 's');
    const fw::ReplayRefusal r{
        .why = app::PlayRefusal::Why::Setting,
        .setting = setting,
        .setting_offered = true
    };
    got.clear();
    {
        StderrTo cap;
        char buf[1400];
        const char* const line = fw::tasty_refusal_sentence(r, {}, false, buf);
        fw::tasty_say(std::string_view{line});
        got = cap.text();
    }
    old.clear();
    {
        char buf[1400];
        std::snprintf(
            buf,
            sizeof buf,
            "replay refused: tasty could not set the core's %.*s for this run",
            static_cast<int>(setting.size()),
            setting.c_str()
        );
        StderrTo cap;
        fw::tasty_say(std::string_view{buf});
        old = cap.text();
    }
    CHECK_EQ_STR(got, old);
    CHECK_EQ(got.size(), std::size_t{1400});

    {
        StderrTo cap;
        fw::TastyRefusal{fw::TastyRefusal::LeadOutOfRange}.say();
        CHECK_EQ_STR(cap.text(), "");
    }

    Tmp t("/tasty_tasty_say_");
    CHECK(!t.root.empty());
    const fw::TastyRuntime rt = runtime_in(t.root);
    CHECK_EQ_STR(fw::TastyPidFile{rt.paths}.busy_text(), "tasty is already playing");
}

void test_owner_second_sigterm_homes_and_exits() {
    if (mister::testing::skipped_under_qemu("the re-exec'd probe never writes its file"))
        return;
    HomeMarker marker;
    CHECK(marker.ok);
    if (!marker.ok)
        return;
    const ChildEnd end = in_child([&] {
        (void)::alarm(2);
        fw::ReturnHome home{{.image = {marker.exe, marker.arg}}};
        fw::HomeLane::owe();
        fw::tasty_arm_owner_signals();
        ::raise(SIGTERM);
        if (marker.started())
            return 2;
        if (fw::tasty_owner_stop_flag().load(std::memory_order_seq_cst) == 0)
            return 3;
        ::raise(SIGTERM);
        return 4;
    });
    CHECK(end.exited && end.code == 128 + SIGTERM);
    CHECK(marker.wait_started());
}

void test_owner_sigbus_homes() {
    if (mister::testing::skipped_under_qemu("the re-exec'd probe never writes its file"))
        return;
    HomeMarker marker;
    CHECK(marker.ok);
    if (!marker.ok)
        return;
    const ChildEnd end = in_child([&] {
        struct rlimit rl{};
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        (void)::setrlimit(RLIMIT_CORE, &rl);
        (void)::alarm(2);
        fw::ReturnHome home{{.image = {marker.exe, marker.arg}}};
        fw::HomeLane::owe();
        fw::tasty_arm_owner_signals();
        ::raise(SIGBUS);
        return 5;
    });
    const bool died = (end.signaled && end.sig == SIGBUS)
        || (end.exited && end.code == 128 + SIGBUS);
    CHECK(died);
    CHECK(marker.wait_started());
}

const char* stock_comm() {
    static char name[16] = {};
    if (name[0] == '\0')
        std::snprintf(name, sizeof name, "MiSTer%d", static_cast<int>(::getpid()));
    return name;
}

int count_mister_comm() {
    DIR* d = ::opendir("/proc");
    if (d == nullptr)
        return -1;
    int n = 0;
    while (const dirent* e = ::readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9')
            continue;
        const int pid = std::atoi(e->d_name);
        if (pid <= 0)
            continue;
        char path[64];
        std::snprintf(path, sizeof path, "/proc/%d/comm", pid);
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        char buf[32]{};
        const ssize_t got = ::read(fd, buf, sizeof buf - 1);
        ::close(fd);
        if (got <= 0)
            continue;
        std::string_view s{buf, static_cast<std::size_t>(got)};
        if (!s.empty() && s.back() == '\n')
            s.remove_suffix(1);
        if (s == stock_comm())
            ++n;
    }
    ::closedir(d);
    return n;
}

bool set_mister_comm() {
    const int fd = ::open("/proc/self/comm", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const std::size_t len = std::strlen(stock_comm());
    const ssize_t w = ::write(fd, stock_comm(), len);
    ::close(fd);
    return w == static_cast<ssize_t>(len);
}

bool is_zombie(pid_t pid) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/stat", static_cast<int>(pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    char buf[256];
    const ssize_t n = ::read(fd, buf, sizeof buf - 1);
    ::close(fd);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    const char* r = std::strrchr(buf, ')');
    return r != nullptr && r[1] == ' ' && r[2] == 'Z';
}

struct MisterHold {
    pid_t pid = -1;
    std::thread reaper;
    MisterHold() = default;
    MisterHold(const MisterHold&) = delete;
    MisterHold& operator=(const MisterHold&) = delete;

    ~MisterHold() {
        if (pid <= 0)
            return;
        (void)::kill(pid, SIGKILL);
        if (reaper.joinable()) {
            reaper.join();
            return;
        }
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {
        }
    }
};

struct BoardShare {
    std::atomic<int> second_sent{0};

    std::atomic<int> stopping{0};
};

static_assert(std::atomic<int>::is_always_lock_free);

struct ShareMap {
    BoardShare* p = nullptr;

    ShareMap() {
        void* m = ::mmap(
            nullptr,
            sizeof(BoardShare),
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS,
            -1,
            0
        );
        if (m == MAP_FAILED)
            return;
        p = new (m) BoardShare();
    }

    ~ShareMap() {
        if (p == nullptr)
            return;
        p->~BoardShare();
        (void)::munmap(p, sizeof(BoardShare));
    }

    ShareMap(const ShareMap&) = delete;
    ShareMap& operator=(const ShareMap&) = delete;
};

bool no_mister_yet() {
    const int already = count_mister_comm();
    if (already != 0)
        std::printf("stop test would signal %d process(es) named MiSTer\n", already);
    CHECK(already == 0);
    return already == 0;
}

void test_second_sigterm_during_stop_homes_after_exit() {
    if (!no_mister_yet())
        return;
    if (mister::testing::skipped_under_qemu("the re-exec'd probe never writes its file"))
        return;
    HomeMarker marker;
    CHECK(marker.ok);
    if (!marker.ok)
        return;
    ShareMap share;
    CHECK(share.p != nullptr);
    if (share.p == nullptr)
        return;
    int ready[2] = {-1, -1};
    int gone[2] = {-1, -1};
    if (::pipe(ready) != 0 || ::pipe(gone) != 0) {
        CHECK(false);
        return;
    }
    MisterHold hold;
    hold.pid = ::fork();
    CHECK(hold.pid >= 0);
    if (hold.pid < 0)
        return;
    if (hold.pid == 0) {
        ::close(ready[0]);
        ::close(gone[0]);
        if (!set_mister_comm())
            ::_exit(9);
        struct sigaction ign{};
        ign.sa_handler = SIG_IGN;
        ::sigemptyset(&ign.sa_mask);
        (void)::sigaction(SIGTERM, &ign, nullptr);
        const char up = 1;
        if (::write(ready[1], &up, 1) != 1)
            ::_exit(9);
        ::close(ready[1]);
        while (share.p->second_sent.load(std::memory_order_acquire) == 0)
            ::usleep(1000);
        ::usleep(300 * 1000);
        if (::write(gone[1], &up, 1) != 1)
            ::_exit(10);
        ::_exit(0);
    }
    ::close(ready[1]);
    ::close(gone[1]);
    char up = 0;
    CHECK(::read(ready[0], &up, 1) == 1);
    ::close(ready[0]);
    if (up != 1)
        return;
    const pid_t owner = ::fork();
    CHECK(owner >= 0);
    if (owner < 0)
        return;
    if (owner == 0) {
        (void)::alarm(12);
        fw::tasty_arm_owner_signals();
        fw::ReturnHome home{{.image = {marker.exe, marker.arg}}};
        BoardShare* board = share.p;
        std::thread poke([board] {
            ::usleep(100 * 1000);
            ::kill(::getpid(), SIGTERM);
            for (int i = 0;
                 i < 200 && fw::tasty_owner_stop_flag().load(std::memory_order_seq_cst) == 0;
                 ++i)
                ::usleep(1000);
            if (!fw::HomeLane::may_launch() && fw::HomeLane::lane() == fw::HomeLane::Lane::Stopping)
                board->stopping.store(1, std::memory_order_release);
            ::kill(::getpid(), SIGTERM);
            board->second_sent.store(1, std::memory_order_release);
        });
        const auto rc = fw::HomeLane::stop_and_owe(
            fw::StockControl{*os::ProcessName::of(stock_comm())}
        );
        if (poke.joinable())
            poke.join();
        ::_exit(rc.has_value() ? 4 : 5);
    }
    hold.reaper = std::thread([pid = hold.pid] {
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {
        }
    });
    int st = 0;
    while (::waitpid(owner, &st, 0) < 0 && errno == EINTR) {
    }
    (void)::fcntl(gone[0], F_SETFL, O_NONBLOCK);
    char b = 0;
    const ssize_t got = ::read(gone[0], &b, 1);
    ::close(gone[0]);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 128 + SIGTERM);
    CHECK(marker.wait_started());
    CHECK(share.p->stopping.load(std::memory_order_acquire) == 1);
    CHECK(got == 1);
}

void test_stop_wait_timeout_launches_nothing() {
    if (!no_mister_yet())
        return;
    SharedLaunch n;
    CHECK(n.p != nullptr);
    if (n.p == nullptr)
        return;
    const ChildEnd end = in_child([&] {
        const pid_t z = ::fork();
        if (z < 0)
            return 2;
        if (z == 0) {
            if (!set_mister_comm())
                ::_exit(9);
            ::_exit(0);
        }
        struct Reap {
            pid_t pid;
            ~Reap() {
                if (pid <= 0)
                    return;
                int st = 0;
                (void)::kill(pid, SIGKILL);
                while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {
                }
            }
        } reap{z};
        bool dead = false;
        for (int i = 0; i < 1000 && !dead; ++i) {
            dead = is_zombie(z);
            if (!dead)
                ::usleep(1000);
        }
        if (!dead)
            return 3;
        int sp[2] = {-1, -1};
        if (::pipe(sp) != 0)
            return 4;
        const int saved = ::dup(STDERR_FILENO);
        if (::dup2(sp[1], STDERR_FILENO) < 0)
            return 5;
        auto rc = mister::Ex<void>{};
        {
            fw::ReturnHome home{{.probe = true}};
            rc = fw::HomeLane::stop_and_owe(fw::StockControl{*os::ProcessName::of(stock_comm())});
        }
        *n.p = fw::HomeLane::launches();
        if (saved >= 0) {
            (void)::dup2(saved, STDERR_FILENO);
            ::close(saved);
        }
        ::close(sp[1]);
        char msg[200]{};
        const ssize_t nr = ::read(sp[0], msg, sizeof msg - 1);
        ::close(sp[0]);
        if (nr < 0)
            return 8;
        if (*n.p != LaunchCount{})
            return 6;
        if (rc.has_value())
            return 7;
        const auto why = fw::TastyRefusal::of(rc.error());
        if (!why || why->why() != fw::TastyRefusal::MenuLeftRunning)
            return 11;
        if (std::strstr(msg, "the menu was left running") == nullptr)
            return 8;

        if (fw::HomeLane::may_launch())
            return 9;
        if (fw::HomeLane::lane() != fw::HomeLane::Lane::Left)
            return 10;
        return 0;
    });
    CHECK(end.exited && end.code == 0);
    CHECK(*n.p == LaunchCount{});
}

namespace {
int g_termed_fd = -1;
}

extern "C" void note_term(int) {
    const char b = 1;
    (void)!::write(g_termed_fd, &b, 1);
}

void test_sigbus_during_stop_does_not_home() {
    if (!no_mister_yet())
        return;
    if (mister::testing::skipped_under_qemu("the re-exec'd probe never writes its file"))
        return;
    HomeMarker marker;
    CHECK(marker.ok);
    if (!marker.ok)
        return;
    int ready[2] = {-1, -1};
    if (::pipe(ready) != 0) {
        CHECK(false);
        return;
    }
    int termed[2] = {-1, -1};
    if (::pipe(termed) != 0) {
        CHECK(false);
        return;
    }
    MisterHold hold;
    hold.pid = ::fork();
    CHECK(hold.pid >= 0);
    if (hold.pid < 0)
        return;
    if (hold.pid == 0) {
        ::close(ready[0]);
        ::close(termed[0]);
        if (!set_mister_comm())
            ::_exit(9);
        g_termed_fd = termed[1];
        struct sigaction note{};
        note.sa_handler = &note_term;
        ::sigemptyset(&note.sa_mask);
        (void)::sigaction(SIGTERM, &note, nullptr);
        const char up = 1;
        if (::write(ready[1], &up, 1) != 1)
            ::_exit(9);
        ::close(ready[1]);
        for (;;)
            ::pause();
    }
    ::close(ready[1]);
    ::close(termed[1]);
    char up = 0;
    CHECK(::read(ready[0], &up, 1) == 1);
    ::close(ready[0]);
    if (up != 1) {
        ::close(termed[0]);
        return;
    }
    const pid_t owner = ::fork();
    CHECK(owner >= 0);
    if (owner < 0) {
        ::close(termed[0]);
        return;
    }
    if (owner == 0) {
        ::close(termed[0]);
        struct rlimit rl{};
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        (void)::setrlimit(RLIMIT_CORE, &rl);
        (void)::alarm(8);
        fw::tasty_arm_owner_signals();
        fw::ReturnHome home{{.image = {marker.exe, marker.arg}}};
        const auto rc = fw::HomeLane::stop_and_owe(
            fw::StockControl{*os::ProcessName::of(stock_comm())}
        );
        ::_exit(rc.has_value() ? 4 : 5);
    }

    pollfd pf{termed[0], POLLIN, 0};
    const bool in_wait = ::poll(&pf, 1, 10000) == 1;
    CHECK(in_wait);
    ::close(termed[0]);
    (void)::kill(owner, SIGBUS);
    int st = 0;
    while (::waitpid(owner, &st, 0) < 0 && errno == EINTR) {
    }
    const bool died = (WIFSIGNALED(st) && WTERMSIG(st) == SIGBUS)
        || (WIFEXITED(st) && WEXITSTATUS(st) == 128 + SIGBUS);
    CHECK(died);
    CHECK(!marker.wait_started(100));
}

void test_prepare_fails_when_the_pid_file_cannot_be_written() {
    Tmp t("/tasty_tasty_d4_");
    CHECK(!t.root.empty());
    if (t.root.empty())
        return;
    fw::TastyRuntime rt = runtime_in(t.root);
    CHECK(rt.paths.pid.assign(t.p("missing/pid")));

    const auto stock = os::ProcessName::of("tasty-no-stock");
    CHECK(stock.has_value());
    if (!stock)
        return;
    rt.stock = *stock;
    fw::TastySession::RecordOnlyRun run{};
    const ChildEnd end = in_child([&] {
        fw::ReplayMode mode{fw::TastyArgs{.verb = fw::TastyVerb::Status}, run, std::nullopt, rt};
        const auto rc = mode.prepare();
        if (!rc || *rc != 1)
            ::_exit(11);
        if (fw::HomeLane::lane() != fw::HomeLane::Lane::Open)
            ::_exit(12);
        ::_exit(1);
        return 0;
    });
    CHECK(end.exited && end.code == 1);
}

[[noreturn]] void write_signal_probe(const char* path) {
    FILE* out = std::fopen(path, "w");
    if (out == nullptr)
        ::_exit(3);

    struct Row {
        int sig;
        const char* name;
    };

    const Row rows[] = {
        {SIGHUP, "hup"},
        {SIGPIPE, "pipe"},
        {SIGINT, "int"},
        {SIGTERM, "term"},
        {SIGQUIT, "quit"},
        {SIGSEGV, "segv"},
        {SIGABRT, "abrt"},
        {SIGBUS, "bus"},
        {SIGUSR1, "usr1"},
        {SIGALRM, "alrm"}
    };
    for (const Row& r : rows) {
        struct sigaction sa{};
        if (::sigaction(r.sig, nullptr, &sa) != 0) {
            std::fprintf(out, "%s err\n", r.name);
            continue;
        }
        const char* disp = "other";
        if (sa.sa_handler == SIG_DFL)
            disp = "dfl";
        else if (sa.sa_handler == SIG_IGN)
            disp = "ign";
        std::fprintf(out, "%s %s\n", r.name, disp);
    }
    sigset_t set;
    ::sigemptyset(&set);
    if (::sigprocmask(SIG_SETMASK, nullptr, &set) != 0) {
        std::fprintf(out, "mask err\n");
    } else {
        int blocked = 0;
        for (int sig = 1; sig < NSIG; ++sig) {
            if (::sigismember(&set, sig) == 1)
                ++blocked;
        }
        std::fprintf(out, "mask %d\n", blocked);
    }
    std::fflush(out);
    std::fclose(out);
    ::_exit(0);
}

bool self_exe(char* dst, std::size_t n) {
    if (n == 0)
        return false;
    const ssize_t got = ::readlink("/proc/self/exe", dst, n - 1);
    if (got <= 0 || static_cast<std::size_t>(got) >= n)
        return false;
    dst[got] = '\0';
    return true;
}

void test_home_exec_resets_signals() {
    if (mister::testing::skipped_under_qemu("the re-exec'd probe never writes its file"))
        return;
    char exe[4096];
    CHECK(self_exe(exe, sizeof exe));
    if (!self_exe(exe, sizeof exe))
        return;
    char path[] = "/tmp/tasty_tasty_sigprobeXXXXXX";
    const int fd = ::mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    ::close(fd);
    ::unlink(path);
    char arg[4200];
    std::snprintf(arg, sizeof arg, "--signal-probe=%s", path);
    const ChildEnd end = in_child([&] {
        fw::tasty_arm_owner_signals();
        sigset_t block;
        ::sigemptyset(&block);
        ::sigaddset(&block, SIGUSR1);
        ::sigaddset(&block, SIGTERM);
        ::sigaddset(&block, SIGALRM);
        if (::sigprocmask(SIG_BLOCK, &block, nullptr) != 0)
            return 2;
        fw::ReturnHome home{{.image = {exe, arg}}};
        fw::HomeLane::launch();
        int st = 0;
        while (::waitpid(-1, &st, 0) < 0) {
            if (errno != EINTR)
                return 4;
        }
        return 0;
    });
    CHECK(end.exited && end.code == 0);
    char text[512]{};
    bool got = false;
    for (int i = 0; i < 250 && !got; ++i) {
        const int rfd = ::open(path, O_RDONLY);
        if (rfd >= 0) {
            const ssize_t n = ::read(rfd, text, sizeof text - 1);
            ::close(rfd);
            if (n > 0 && std::strstr(text, "mask ") != nullptr)
                got = true;
        }
        if (!got)
            ::usleep(20 * 1000);
    }
    ::unlink(path);
    if (!got)
        std::printf("FAIL home probe missing (child %d)\n", end.code);
    CHECK(got);
    if (!got)
        return;

    for (const char* name :
         {"hup ", "pipe ", "int ", "term ", "quit ", "segv ", "abrt ", "bus ", "usr1 ", "alrm "})
        CHECK(std::strstr(text, name) != nullptr);
    CHECK(std::strstr(text, " ign\n") == nullptr);
    CHECK(std::strstr(text, " err\n") == nullptr);
    CHECK(std::strstr(text, "mask 0\n") != nullptr);
}

long long mono_ms(const timespec& a, const timespec& b) {
    long long sec = static_cast<long long>(b.tv_sec) - static_cast<long long>(a.tv_sec);
    long long nsec = static_cast<long long>(b.tv_nsec) - static_cast<long long>(a.tv_nsec);
    if (nsec < 0) {
        --sec;
        nsec += 1000000000LL;
    }
    return sec * 1000 + nsec / 1000000;
}

void test_signal_during_grace_does_not_kill_early() {
    if (!no_mister_yet())
        return;
    int ready[2] = {-1, -1};
    if (::pipe(ready) != 0) {
        CHECK(false);
        return;
    }
    const pid_t mid = ::fork();
    CHECK(mid >= 0);
    if (mid < 0) {
        ::close(ready[0]);
        ::close(ready[1]);
        return;
    }
    if (mid == 0) {
        ::close(ready[0]);
        const pid_t gc = ::fork();
        if (gc < 0)
            ::_exit(9);
        if (gc > 0)
            ::_exit(0);
        if (!set_mister_comm())
            ::_exit(9);
        struct sigaction ign{};
        ign.sa_handler = SIG_IGN;
        ::sigemptyset(&ign.sa_mask);
        (void)::sigaction(SIGTERM, &ign, nullptr);
        const int self = ::getpid();
        if (::write(ready[1], &self, sizeof self) != static_cast<ssize_t>(sizeof self))
            ::_exit(9);
        ::close(ready[1]);
        for (;;)
            ::pause();
    }
    ::close(ready[1]);
    int mid_st = 0;
    while (::waitpid(mid, &mid_st, 0) < 0 && errno == EINTR) {
    }
    int stock = -1;
    const ssize_t nr = ::read(ready[0], &stock, sizeof stock);
    ::close(ready[0]);
    CHECK(nr == static_cast<ssize_t>(sizeof stock) && stock > 0);
    if (nr != static_cast<ssize_t>(sizeof stock) || stock <= 0)
        return;

    struct KillPid {
        int pid;

        ~KillPid() {
            if (pid > 0)
                (void)::kill(pid, SIGKILL);
        }
    } hold{stock};

    const ChildEnd end = in_child([] {
        (void)::alarm(8);
        fw::tasty_arm_owner_signals();
        sigset_t block;
        ::sigemptyset(&block);
        ::sigaddset(&block, SIGTERM);
        std::thread poke([&block] {
            (void)::pthread_sigmask(SIG_BLOCK, &block, nullptr);
            ::usleep(200 * 1000);
            (void)::kill(::getpid(), SIGTERM);
        });
        timespec t0{};
        timespec t1{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &t0);
        const auto stopped = fw::StockControl{*os::ProcessName::of(stock_comm())}.stop();
        (void)::clock_gettime(CLOCK_MONOTONIC, &t1);
        const long long ms = mono_ms(t0, t1);
        if (poke.joinable())
            poke.join();
        if (ms < 1500)
            return 2;
        if (!stopped || !*stopped)
            return 3;
        return 0;
    });
    if (!(end.exited && end.code == 0))
        std::printf(
            "FAIL grace exited=%d code=%d signaled=%d sig=%d\n",
            static_cast<int>(end.exited),
            end.code,
            static_cast<int>(end.signaled),
            end.sig
        );
    CHECK(end.exited && end.code == 0);
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strncmp(argv[1], "--signal-probe=", 15) == 0)
        write_signal_probe(argv[1] + 15);
    test_cli_play();
    test_cli_vsync();
    test_cli_rec();
    test_cli_record_options();
    test_session_passes_record_options();
    test_cli_record_empty();
    test_cli_hashes();
    test_boot_no_handoff();
    test_cli_play_needs_rom();
    test_cli_loop_and_ram_init();
    test_cli_save_is_play_only();
    test_save_seed_copied();
    test_save_seed_refusals();
    test_manifest_doc_role_follows_the_core_set();
    test_registry();
    test_codec_path();
    test_null_osd();
    test_movie_system_path();
    test_splash_pixel();
    test_replay_end_names();
    test_lock_paths();
    test_return_home();
    test_return_home_error_path();
    test_home_lane_claims_once_per_bind();
    test_run_exit_statuses();
    test_home_lane_launch_count_shared_with_child();
    test_home_lane_unbind_clears_the_probe();
    test_owner_sighup_keeps_running();
    test_owner_sigterm_ends_and_homes_once();
    test_owner_closed_stdio_survives();
    test_tasty_render_matches_snprintf();
    test_converted_user_lines_are_unchanged();
    test_owner_second_sigterm_homes_and_exits();
    test_owner_sigbus_homes();
    test_second_sigterm_during_stop_homes_after_exit();
    test_stop_wait_timeout_launches_nothing();
    test_sigbus_during_stop_does_not_home();
    test_prepare_fails_when_the_pid_file_cannot_be_written();
    test_home_exec_resets_signals();
    test_signal_during_grace_does_not_kill_early();
    test_lock_unlink();
    test_two_owner_locks_over_different_paths_coexist();
    test_status_tmp_path_is_not_clipped();
    test_record_dir();
    test_prepare_record_stem();
    test_prepare_record_avi_stem();
    test_prepare_record_says_why();
    test_resolve_relative();
    test_info_missing();
    test_info_frames_and_rerecords();
    test_info_reads_an_overlong_header_line_as_the_feeder_does();
    test_info_reads_an_unterminated_last_header_line();
    test_check_missing();
    test_busy_text();
    test_stop_idle();
    test_stop_owner_signaled();
    test_stop_stale_pid();
    test_rec_start_checks_the_owner();
    test_load_edge_rereads_geometry();
    test_unhandled_events_are_counted_silently();
    test_start_waits_for_video_edge();
    test_strict_rec_start_waits_for_the_ini();
    test_take_play_refusal();
    test_a_port_that_forgets_a_verb_is_abstract();
    test_a_core_ask_on_a_full_ring_is_retried();
    test_a_taken_core_ask_is_not_repeated();
    test_a_menu_ask_on_a_full_ring_is_retried();
    test_later_refusal_ends_session();
    test_epoch_ambiguous_then_finished();
    test_epoch_ambiguous_final_leaves();
    test_replay_end_sentences();
    test_loop_restarts_after_finish();
    test_loop_wins_over_stay();
    test_session_passes_ram_init();
    test_prepare_play_lead_rom_and_ram();
    test_tasty_refusal_round_trips();
    test_prepare_play_names_its_refusal();
    test_rom_search_stop_sentence_end_to_end();
    test_loop_with_record_is_refused();
    test_rerecords_need_a_whole_decimal();
    test_ram_fill_is_the_codecs();
    test_replay_ends_leave();
    test_stay_finished_holds();
    test_stay_stop_leaves();
    test_rec_only_stop_leaves();
    test_rec_only_leaves_when_its_recording_ends();
    test_rec_only_reports_a_failure_after_it_started();
    test_rec_only_a_stopped_recording_after_it_started_is_clean();
    test_a_recorded_replay_reports_a_failure_while_it_runs();
    test_a_recorded_replay_reports_a_failure_as_it_closes();
    test_rec_only_probe_refusal_fails_the_run();
    test_a_stuck_port_ends_a_replay();
    test_the_verdict_line_names_the_remedy();
    test_record_path_outside_the_root_is_refused();
    test_stale_generation();
    test_status_force();
    test_status_frames_total();
    test_status_scale_step_warns_once();
    test_status_scale_return_is_said();
    test_status_scale_step_warns_per_recording();
    test_check_exits();
    test_cli_run_flags();
    test_session_play_ask();
    test_session_prints_settings_once();
    test_session_direct_video_force();
    test_session_hides_the_osd_first();
    test_refusal_sentences();
    test_movie_refusals_have_sentences();
    test_session_waits_for_the_osd_hide();
    test_recorded_replay_rereads_the_geometry();
    test_unrecorded_replay_leaves_the_geometry();
    test_bk2_sniff();
    test_bk2_unknown_platform_refused();
    test_nes_bk2_check_header_forms();
    if (failures) {
        std::printf("tasty_unit: %d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("tasty_unit: all checks passed\n");
    return 0;
}
