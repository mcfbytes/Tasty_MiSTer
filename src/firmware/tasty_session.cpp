// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_session.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/durable_write.h"
#include "app/rbf_resolve.h"
#include "app/replay_status.h"
#include "app/recorder_control.h"
#include "app/recorder_status.h"
#include "app/replay_feeder.h"
#include "app/session_identity.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/xml_kind.h"
#include "app/video_pump.h"
#include "app/identity_latch.h"
#include "app/link_tx_channel.h"
#include "app/output_size.h"
#include "cores/registry.h"
#include "infra/json_out.h"
#include "infra/json_width.h"
#include "report.h"
#include "proto/link_op.h"
#include "svc/vfs.h"
#include "tasty_registry.h"
#include "tasty_ctl.h"

namespace mister::fw {
namespace {

constexpr std::chrono::nanoseconds kCoreLoadTimeout{std::chrono::seconds{30}};
constexpr std::chrono::nanoseconds kMenuLoadTimeout{std::chrono::seconds{20}};
constexpr std::chrono::milliseconds kStatusPeriod{250};

constexpr unsigned kHideTries = 50;

constexpr app::MovieFrame kResampleFrame{60};

constexpr const char*
    kStatusLineFormat = "{\"play\":\"%s\",\"end\":\"%s\",\"frame\":%s,\"frames\":%s,\"rec\":\"%s\","
                        "\"rows\":%s,\"late\":%u,\"underruns\":%u,\"skipped\":%u,\"drops\":%u,"
                        "\"missed\":%s,\"scale\":%u}\n";

constexpr const char* kPlayIdle = "idle";
constexpr const char* kPlayArming = "arming";
constexpr const char* kPlayRunning = "running";
constexpr std::array<const char*, 3> kPlayLevelWords{kPlayIdle, kPlayArming, kPlayRunning};

template <class Rep>
[[nodiscard]] consteval infra::JsonWidth width_of() noexcept {
    return infra::JsonWidth::digits<Rep>();
}

template <class Text>
[[nodiscard]] consteval bool status_line_fits() noexcept {

    std::size_t punctuation = 0;
    constexpr std::string_view format{kStatusLineFormat};
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%') {
            ++i;
            continue;
        }
        ++punctuation;
    }
    constexpr infra::JsonWidth play = infra::json_longest(kPlayLevelWords);
    constexpr infra::JsonWidth end = infra::json_longest(app::kReplayEndNames);
    constexpr infra::JsonWidth frame = width_of<decltype(app::MovieFrame::v)>();
    constexpr infra::JsonWidth frames = width_of<decltype(app::FrameCount::v)>();
    constexpr infra::JsonWidth rec = infra::json_longest(app::kRecStateNames);
    constexpr infra::JsonWidth rows = width_of<decltype(app::CoreFrameCount::v)>();
    constexpr infra::JsonWidth late = width_of<decltype(app::ReplayStatus::late)>();
    constexpr infra::JsonWidth underruns = width_of<decltype(app::ReplayStatus::underruns)>();
    constexpr infra::JsonWidth skipped = width_of<decltype(app::ReplayStatus::skipped_edges)>();
    constexpr infra::JsonWidth drops = width_of<decltype(app::ReplayStatus::pad_drops)>();
    constexpr infra::JsonWidth missed = width_of<decltype(app::CoreFrameCount::v)>();
    constexpr infra::JsonWidth scale = width_of<unsigned>();
    const infra::JsonWidth worst = infra::JsonWidth{punctuation}
        + play
        + end
        + frame
        + frames
        + rec
        + rows
        + late
        + underruns
        + skipped
        + drops
        + missed
        + scale;
    return worst < infra::JsonWidth{Text::kBufSize};
}

}

TastySession::TastySession(const Wiring& w, const TastyArgs& args) noexcept : w_(w), args_(args) {
    rec_only_ = args_.verb == TastyVerb::RecStart;
    if (!args_.core.empty())
        (void)core_path_.assign(args_.core.view());
    std::string menu = std::string(w_.vfs.root_path()) + "/" + kMenuRbfName;
    (void)menu_path_.assign(menu);
    if (!args_.movie.empty()) {
        if (auto s = cores::movie_system_for(w_.vfs, args_.movie.view()))
            sys_ = *s;
    }
    if (core_path_.empty()
        && cores::movie_system_supported(sys_)
        && tasty_plays(sys_.conf_str_name)) {
        const std::string dir = std::string(w_.vfs.root_path()) + "/_Console";
        if (auto n = app::resolve_rbf_name(w_.vfs, dir, sys_.conf_str_name, false); n) {
            const std::string p = dir + "/" + *n;
            (void)core_path_.assign(p);
        }
    }
}

bool TastySession::core_ready_() const noexcept {
    if (w_.identity.generation() == id_gen_)
        return false;
    const std::optional<app::SessionIdentity> id = w_.identity.copy();
    if (!id)
        return false;
    if (sys_.conf_str_name.empty())
        return true;
    return id->rbf.view() == sys_.conf_str_name;
}

bool TastySession::video_reloaded_() const noexcept {
    return !w_.video || w_.video->stats().edges != video_edges_;
}

bool TastySession::menu_ready_() const noexcept {
    if (id_gen_ != 0 && w_.identity.generation() == id_gen_)
        return false;
    const std::optional<app::SessionIdentity> id = w_.identity.copy();
    if (!id)
        return false;
    return cores::profile_for(id->rbf.view()).is_front_end;
}

void TastySession::ask_core_(std::string_view path) noexcept {
    if (path.empty())
        return;
    id_gen_ = w_.identity.generation();
    if (w_.video)
        video_edges_ = w_.video->stats().edges;
    app::UiRequest::LoadCore req{};
    req.xml = app::XmlKind::Rbf;
    if (!req.path.assign(path))
        return;
    (void)w_.asks.push(req);
}

bool TastySession::copy_seed_() noexcept {
    if (!args_.save)
        return false;
    auto in = w_.vfs.open(args_.save->view(), svc::OpenMode::ReadWhole);
    const auto sz = in ? (*in)->size() : Ex<svc::FileSize>{std::unexpected(in.error())};
    if (!sz || *sz > kSeedMax)
        return false;
    std::vector<std::byte> bytes;
    bytes.resize(static_cast<std::size_t>(sz->v));
    const auto got = (*in)->read_at(0, bytes);
    if (!got || *got != bytes.size())
        return false;
    if (!w_.vfs.ensure_dir(kTastySaveRoot))
        return false;
    return app::durable_write(w_.vfs, std::string(kTastySaveRoot) + "/seed.sav", bytes).has_value();
}

void TastySession::fail_(const char* why) noexcept {
    char buf[1500];
    std::snprintf(buf, sizeof buf, "tasty: %s", why != nullptr ? why : "");
    tasty_say(buf);
    w_.run_exit.publish(mister::fw::refused());
    go_menu_();
}

void TastySession::fail_refusal_() noexcept {
    const std::optional<ReplayRefusal> refusal = w_.replay.refusal();
    char buf[1400];
    if (!refusal) {
        fail_("replay refused");
        return;
    }
    const ReplayRefusal& r = *refusal;
    switch (r.why) {
        case app::PlayRefusal::Why::Setting:
            if (r.setting_offered && args_.strict) {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused (--strict): the core's %.*s is not the movie's; "
                    "without --strict tasty sets it for this run",
                    static_cast<int>(r.setting.size()),
                    r.setting.data()
                );
            } else if (r.setting_offered) {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused: tasty could not set the core's %.*s for this run",
                    static_cast<int>(r.setting.size()),
                    r.setting.data()
                );
            } else {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused: this movie needs a %.*s this core does not offer",
                    static_cast<int>(r.setting.size()),
                    r.setting.data()
                );
            }
            break;
        case app::PlayRefusal::Why::Firmware:
            if (!r.path.empty()) {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused: the movie was recorded with another BIOS; it "
                    "needs its own at %.*s",
                    static_cast<int>(r.path.size()),
                    r.path.data()
                );
            } else {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused: the movie was recorded with a BIOS; put it beside "
                    "the disc image"
                );
            }
            break;
        case app::PlayRefusal::Why::Movie:
            switch (r.movie.value_or(cores::MovieRefusal::None)) {
                case cores::MovieRefusal::Savestate: {
                    const std::string_view movie = args_.movie.view();
                    std::snprintf(
                        buf,
                        sizeof buf,
                        "replay refused: %.*s starts from a savestate or saved game, "
                        "not from power-on",
                        static_cast<int>(movie.size()),
                        movie.data()
                    );
                    break;
                }
                case cores::MovieRefusal::NotAMovie:
                case cores::MovieRefusal::Binary:
                case cores::MovieRefusal::Disk:
                case cores::MovieRefusal::PortType:
                case cores::MovieRefusal::Multitap:
                case cores::MovieRefusal::Checksum:
                case cores::MovieRefusal::Command:
                case cores::MovieRefusal::TooLong:
                case cores::MovieRefusal::BadLine:
                case cores::MovieRefusal::Setting:
                case cores::MovieRefusal::Archive:
                case cores::MovieRefusal::Subframe:
                case cores::MovieRefusal::System:
                case cores::MovieRefusal::ChdUnsupported: {
                    const char* plain = tasty_movie_refusal_sentence(
                        r.movie.value_or(cores::MovieRefusal::None)
                    );
                    std::snprintf(
                        buf,
                        sizeof buf,
                        "replay refused: %s",
                        plain != nullptr ? plain : app::PlayRefusal::name(r.why)
                    );
                    break;
                }
                case cores::MovieRefusal::None:
                case cores::MovieRefusal::kCount:
                    std::snprintf(
                        buf,
                        sizeof buf,
                        "replay refused: %s",
                        app::PlayRefusal::name(r.why)
                    );
                    break;
            }
            break;
        case app::PlayRefusal::Why::None:
        case app::PlayRefusal::Why::Busy:
        case app::PlayRefusal::Why::NotWired:
        case app::PlayRefusal::Why::Slot:
        case app::PlayRefusal::Why::PowerOn:
            if (r.setting.empty()) {
                std::snprintf(buf, sizeof buf, "replay refused: %s", app::PlayRefusal::name(r.why));
            } else {
                std::snprintf(
                    buf,
                    sizeof buf,
                    "replay refused: %s %.*s",
                    app::PlayRefusal::name(r.why),
                    static_cast<int>(r.setting.size()),
                    r.setting.data()
                );
            }
            break;
        case app::PlayRefusal::Why::NoCodec:
        case app::PlayRefusal::Why::MovieIo:
        case app::PlayRefusal::Why::RomIo:
        case app::PlayRefusal::Why::RomSize:
        case app::PlayRefusal::Why::Checksum:
        case app::PlayRefusal::Why::Phase:
        case app::PlayRefusal::Why::Lead:
        case app::PlayRefusal::Why::Command:
        case app::PlayRefusal::Why::TooLong:
        case app::PlayRefusal::Why::RomKind:
        case app::PlayRefusal::Why::RomChd: {
            const char* plain = tasty_refusal_sentence(r.why);
            std::snprintf(
                buf,
                sizeof buf,
                "replay refused: %s",
                plain != nullptr ? plain : app::PlayRefusal::name(r.why)
            );
            break;
        }
        case app::PlayRefusal::Why::kCount:
            std::snprintf(buf, sizeof buf, "replay refused");
            break;
    }
    fail_(buf);
}

TastySession::Stage TastySession::after_hide_() const noexcept {
    if (!args_.no_splash)
        return Stage::SplashMap;
    return rec_only_ ? Stage::Start : Stage::LoadCore;
}

std::optional<std::uint8_t> TastySession::direct_video_resolved_() const noexcept {
    if (w_.video && !w_.video->ini_read())
        return std::nullopt;
    if (w_.direct_video_ini.generation() == 0)
        return std::nullopt;
    std::uint8_t was = 0;
    if (w_.video) {
        const std::uint8_t pump = w_.video->direct_video_ini();
        if (pump > was)
            was = pump;
    }
    std::uint8_t cell = 0;
    if (w_.direct_video_ini.sample_into(cell) != 0 && cell > was)
        was = cell;
    return was;
}

void TastySession::print_settings_() noexcept {
    if (!direct_video_line_ && !args_.strict && direct_video_was_ != 0) {
        std::fprintf(
            stderr,
            "tasty: direct_video: using 0 for this run (your setting: %u)\n",
            static_cast<unsigned>(direct_video_was_)
        );
        direct_video_line_ = true;
    }
    const std::size_t n = w_.replay.settings_set();
    for (; settings_printed_ < n; ++settings_printed_) {
        const std::string line = w_.replay.setting_line(settings_printed_);
        char buf[1500];
        std::snprintf(buf, sizeof buf, "tasty: %s", line.c_str());
        tasty_say(buf);
    }
}

void TastySession::go_menu_() noexcept {
    stage_ = Stage::LoadMenu;
    stage_ns_ = {};
}

bool TastySession::rec_only_over_() noexcept {
    app::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return false;

    if (rec.state != app::RecState::Idle) {
        if (rec.state != app::RecState::Probing)
            rec_seen_ = true;
        return false;
    }
    const app::RecVerdict verdict = w_.rec.last_verdict();
    if (verdict == app::RecVerdict::Started)
        rec_seen_ = true;
    const bool recording_refused = verdict != app::RecVerdict::None
        && verdict != app::RecVerdict::Started;
    if (recording_refused && !rec_seen_) {
        char buf[160];
        std::snprintf(
            buf,
            sizeof buf,
            "tasty: the recording did not start (%s)",
            app::rec_verdict_name(verdict)
        );
        tasty_say(buf);
        w_.run_exit.publish(refused());
    }
    note_rec_failure_(rec);
    return rec_seen_ || recording_refused;
}

void TastySession::sample_rec_failure_() noexcept {
    if (!args_.record || args_.record->empty())
        return;
    app::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return;
    if (rec.state != app::RecState::Idle && rec.state != app::RecState::Probing)
        rec_seen_ = true;
    note_rec_failure_(rec);
}

void TastySession::note_rec_failure_(const app::RecorderStatus& rec) noexcept {
    if (rec_failure_said_ || !rec_seen_)
        return;
    if (rec.state != app::RecState::Idle)
        return;
    if (rec.end != app::RecVerdict::WriterFailed && rec.end != app::RecVerdict::NoMemory)
        return;
    rec_failure_said_ = true;
    char buf[192];
    std::snprintf(
        buf,
        sizeof buf,
        "tasty: the recording failed after it started (%s): the AVI may be incomplete",
        app::rec_verdict_name(rec.end)
    );
    tasty_say(buf);
    w_.run_exit.publish(refused());
}

namespace {

void say_scale_step(const app::EncodeStatus::Video& v) noexcept {
    const char* why = !v.fell_behind ? "the encoder was using too much CPU"
        : v.held                     ? "the encoder fell behind again soon after catching up"
                                     : "the encoder fell behind";
    const char* until = v.fell_behind && !v.held ? " until it catches up" : "";
    std::fprintf(
        stderr,
        "tasty: %s; the AVI continues at half size in a new segment%s (--scale native "
        "keeps full size and may repeat frames instead)\n",
        why,
        until
    );
}

void say_scale_return() noexcept {
    std::fprintf(
        stderr,
        "tasty: the encoder caught up; the AVI is back at full size in a new segment\n"
    );
}

}

bool TastySession::scaler_port_stuck_() const noexcept {
    app::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return false;
    return rec.end == app::RecVerdict::ScalerPortStuck;
}

void TastySession::write_status_(bool force) noexcept {

    static_assert(status_line_fits<StatusText>(), "the widest status line must fit its one buffer");
    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();
    if (!force
        && last_status_ns_ != std::chrono::nanoseconds::zero()
        && now - last_status_ns_ < kStatusPeriod)
        return;
    app::ReplayStatus play{};
    app::RecorderStatus rec{};
    (void)w_.play.sample_into(play);
    (void)w_.recstat.sample_into(rec);
    app::EncodeStatus enc{};
    (void)w_.encstat.sample_into(enc);
    if (said_gen_ != rec.gen) {
        said_gen_ = rec.gen;
        said_steps_ = {};
        said_recovered_ = {};
    }

    const bool stepped = enc.video.steps > said_steps_;
    const bool returned = enc.video.recovered > said_recovered_;
    said_steps_ = enc.video.steps;
    said_recovered_ = enc.video.recovered;
    if (returned && enc.video.scale == app::PictureScale::Half)
        say_scale_return();
    if (stepped)
        say_scale_step(enc.video);
    if (returned && enc.video.scale != app::PictureScale::Half)
        say_scale_return();
    const app::FrameCount total = w_.replay.movie_frames();
    const char* end = w_.replay.refused() ? "refused" : app::replay_end_name(play.end);
    const proto::FieldRep video_scale = proto::printed_field(
        app::picture_scale_digit(enc.video.scale)
    );
    const char* const level = play.level == app::ReplayLevel::Running ? kPlayRunning
        : play.level == app::ReplayLevel::AwaitPowerOn                ? kPlayArming
                                                                      : kPlayIdle;
    char text[StatusText::kBufSize];
    const int n = std::snprintf(
        text,
        sizeof text,
        kStatusLineFormat,
        level,
        end,
        play.movie_frame.text().c_str(),
        total.text().c_str(),
        app::rec_state_name(rec.state),
        rec.rows.text().c_str(),
        play.late,
        play.underruns,
        play.skipped_edges,
        play.pad_drops,
        rec.missed.text().c_str(),
        video_scale
    );

    if (n <= 0 || std::cmp_greater_equal(n, StatusText::kBufSize))
        return;
    const std::string_view line{text, static_cast<std::size_t>(n)};
    if (!force && last_status_.view() == line) {
        last_status_ns_ = now;
        return;
    }
    if (!tasty_write_status(line))
        return;

    if (!last_status_.assign(line))
        return;
    last_status_ns_ = now;
}

bool TastySession::begin_play_() noexcept {
    const auto dv = direct_video_resolved_();
    if (!dv)
        return false;
    direct_video_was_ = *dv;
    if (args_.strict && direct_video_was_ != 0) {
        fail_(
            "replay refused (--strict): direct_video is not off; without --strict tasty "
            "sets it for this run"
        );
        return false;
    }
    const app::RecMode mode = args_.hashes_only ? app::RecMode::Hash : app::RecMode::Avi;
    if (args_.record && !args_.record->empty() && !play_started_) {
        const std::string_view rec = args_.record->view();
        if (!w_.rec.can_record(rec)) {
            char why[app::kPathMax + 64];
            std::snprintf(
                why,
                sizeof why,
                "cannot record to %.*s: a recording goes under %s",
                static_cast<int>(rec.size()),
                rec.data(),
                app::RecorderControl::kRoot.data()
            );
            fail_(why);
            return false;
        }
        if (args_.verb == TastyVerb::Play) {
            (void)w_.rec.take_arm(rec, mode, args_.rec);
        } else {
            (void)w_.rec.take_start(rec, mode, args_.rec);
        }
    }
    if (!rec_only_) {
        const bool seeded = args_.save.has_value();
        if (seeded && !copy_seed_()) {
            fail_("replay refused: cannot copy the --save file");
            return false;
        }
        const app::ReplayFeeder::Play play{
            .movie = args_.movie.view(),
            .rom = args_.rom.view(),
            .phase = args_.phase_us,
            .lead = args_.lead,
            .stop_at = args_.stop_at,
            .set_settings = !args_.strict,
            .ram_fill = args_.ram_fill,
            .seeded_save = seeded
        };
        if (!w_.replay.take_play(play)) {
            fail_refusal_();
            return false;
        }
        play_started_ = true;
    }
    return true;
}

bool TastySession::run_head_() noexcept {
    print_settings_();
    write_status_();
    if (scaler_port_stuck_()) {
        fail_(app::rec_verdict_remedy(app::RecVerdict::ScalerPortStuck));
        return false;
    }
    if (rec_only_) {
        if (rec_only_over_())
            go_menu_();
        return false;
    }

    sample_rec_failure_();
    if (w_.replay.refused()) {
        fail_refusal_();
        return false;
    }
    return true;
}

void TastySession::advance_splash_(std::chrono::nanoseconds now) noexcept {
    const app::OutputSize out = w_.video ? w_.video->output_size() : app::OutputSize{};
    const app::OutputSize size = out.or_default(
        app::OutputSize{
            app::OutputPixels::of(TastySplash::kDefaultW),
            app::OutputPixels::of(TastySplash::kDefaultH)
        }
    );
    if (stage_ == Stage::SplashMap) {
        splash_ok_ = static_cast<bool>(splash_.map(w_.video_fb));
        stage_ = splash_ok_ ? Stage::SplashBlit : (rec_only_ ? Stage::Start : Stage::LoadCore);
    } else if (stage_ == Stage::SplashBlit) {
        splash_ok_ = splash_.blit(size);
        stage_ = splash_ok_ ? Stage::SplashShow : (rec_only_ ? Stage::Start : Stage::LoadCore);
    } else if (stage_ == Stage::SplashShow) {
        if (!splash_.show(*w_.video, size)) {
            stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
            return;
        }
        stage_ = Stage::SplashWait;
        stage_ns_ = now;
    } else if (stage_ == Stage::SplashWait) {
        if (now - stage_ns_ >= TastySplash::kShowNs)
            stage_ = Stage::SplashHide;
    } else if (stage_ == Stage::SplashHide) {
        (void)splash_.hide(*w_.video);
        stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
    }
}

void TastySession::on_replay_end_(
    app::ReplayEnd end,
    app::EpochFail epoch_fail,
    std::chrono::nanoseconds now
) noexcept {
    if (end == app::ReplayEnd::None || end == app::ReplayEnd::kCount)
        return;
    if (end == app::ReplayEnd::EpochAmbiguous && w_.replay.active())
        return;
    if (end == app::ReplayEnd::Finished && args_.loop) {
        if (w_.replay.active())
            return;
        settings_printed_ = 0;
        direct_video_line_ = false;
        stage_ = Stage::Start;
        return;
    }
    if (end != app::ReplayEnd::Finished && end != app::ReplayEnd::Stopped) {
        char buf[800];
        std::snprintf(buf, sizeof buf, "tasty: %s", app::replay_end_sentence(end, epoch_fail));
        tasty_say(buf);
        w_.run_exit.publish(refused());
    }
    if (args_.stay && end == app::ReplayEnd::Finished)
        return;
    if (end != app::ReplayEnd::Finished) {
        go_menu_();
    } else {
        stage_ = Stage::ReturnWait;
        stage_ns_ = now;
    }
}

void TastySession::tick() noexcept {
    TASTY_SEAT_BODY(TastySession);
    if (stage_ == Stage::HideOsd) {

        const bool sent = w_.link_tx.push(
            proto::LinkOp::SetOsdVisible{.show = proto::LinkOp::OsdShow::Off}
        );
        if (sent || ++hide_tries_ >= kHideTries)
            stage_ = after_hide_();
    }
    if (!w_.video && stage_ >= Stage::SplashMap && stage_ <= Stage::SplashHide) {
        stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
    }
    const bool stop = w_.stop.load(std::memory_order_relaxed) != 0;
    if (stop && stage_ != Stage::LoadMenu && stage_ != Stage::WaitMenu && stage_ != Stage::Done) {
        go_menu_();
    }
    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();
    switch (stage_) {
        case Stage::HideOsd:
            break;
        case Stage::SplashMap:
        case Stage::SplashBlit:
        case Stage::SplashShow:
        case Stage::SplashWait:
        case Stage::SplashHide:
            advance_splash_(now);
            break;
        case Stage::LoadCore:
            if (core_path_.empty()) {
                fail_("no core to load");
                break;
            }
            ask_core_(core_path_.view());
            stage_ = Stage::WaitId;
            stage_ns_ = now;
            break;
        case Stage::WaitId:
            if (core_ready_() && video_reloaded_()) {
                stage_ = Stage::Start;
            } else if (now - stage_ns_ > kCoreLoadTimeout) {
                fail_("core load timed out");
            }
            break;
        case Stage::Start:
            if (!begin_play_())
                break;
            stage_ = Stage::Run;
            break;
        case Stage::Run: {
            if (!run_head_())
                break;
            app::ReplayStatus play{};
            (void)w_.play.sample_into(play);

            if (!resampled_
                && args_.record
                && !args_.record->empty()
                && w_.video
                && play.level == app::ReplayLevel::Running
                && play.movie_frame >= kResampleFrame) {
                w_.video->resample_geometry();
                resampled_ = true;
            }
            on_replay_end_(play.end, play.epoch_fail, now);
            break;
        }
        case Stage::ReturnWait:
            sample_rec_failure_();
            write_status_();
            if (now - stage_ns_ >= args_.linger) {
                go_menu_();
            }
            break;
        case Stage::LoadMenu:
            sample_rec_failure_();
            (void)w_.replay.take_stop();
            (void)w_.rec.take_stop();
            if (!menu_path_.empty())
                ask_core_(menu_path_.view());
            stage_ = Stage::WaitMenu;
            stage_ns_ = now;
            break;
        case Stage::WaitMenu:
            if (menu_ready_() || now - stage_ns_ > kMenuLoadTimeout) {
                write_status_(true);
                stage_ = Stage::Done;
            }
            break;
        case Stage::Done:
            if (!stop_sent_)
                w_.main_stop.request();
            stop_sent_ = true;
            break;
    }
}

}
