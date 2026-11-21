// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_session.h"

#include <chrono>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/rbf_resolve.h"
#include "app/replay_status.h"
#include "app/recorder_control.h"
#include "app/capture/recorder_status.h"
#include "app/replay/replay_feeder.h"
#include "app/session_identity.h"
#include "app/stream_io.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/xml_kind.h"
#include "app/video_pump.h"
#include "app/identity_latch.h"
#include "app/link_tx_channel.h"
#include "app/output_size.h"
#include "app/session_owner.h"
#include "cores/registry.h"
#include "infra/json_out.h"
#include "infra/json_width.h"
#include "infra/prelude.h"
#include "report.h"
#include "proto/link_op.h"
#include "svc/vfs.h"
#include "tasty_registry.h"
#include "tasty_paths.h"
#include "tasty_say.h"
#include "tasty_status_file.h"
#include "tasty_sentences.h"

namespace mister::fw {
namespace {

constexpr std::chrono::nanoseconds kCoreLoadTimeout{std::chrono::seconds{30}};
constexpr std::chrono::nanoseconds kMenuLoadTimeout{std::chrono::seconds{20}};
constexpr std::chrono::milliseconds kStatusPeriod{250};

constexpr unsigned kHideTries = 50;

constexpr app::MoviePosition kResampleFrame{60};

constexpr const char*
    kStatusLineFormat = "{\"play\":\"%s\",\"end\":\"%s\",\"frame\":%s,\"frames\":%s,\"rec\":\"%s\","
                        "\"rows\":%s,\"late\":%u,\"underruns\":%u,\"skipped\":%s,\"drops\":%u,"
                        "\"missed\":%s,\"scale\":%u}\n";

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
    constexpr infra::JsonWidth play = infra::json_longest(app::kReplayLevelNames);
    constexpr infra::JsonWidth end = infra::json_longest(app::kReplayEndNames);
    constexpr infra::JsonWidth frame = width_of<decltype(app::MoviePosition::v)>();
    constexpr infra::JsonWidth frames = width_of<decltype(app::MovieFrameCount::v)>();
    constexpr infra::JsonWidth rec = infra::json_longest(app::capture::kRecStateNames);
    constexpr infra::JsonWidth rows = width_of<decltype(app::CoreFrameCount::v)>();
    constexpr infra::JsonWidth late = width_of<decltype(app::ReplayStatus::late)>();
    constexpr infra::JsonWidth underruns = width_of<decltype(app::ReplayStatus::underruns)>();
    constexpr infra::JsonWidth skipped = width_of<decltype(app::CoreFrameCount::v)>();
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

TastySession::TastySession(const Wiring& w, const TastyArgs& args, const Run& run) noexcept
    : w_(w),
      args_(args),
      run_(run),
      card_(w.vfs) {
    if (!args_.core.empty())
        (void)core_path_.assign(args_.core.view());
    std::string menu = std::string(w_.vfs.root_path()) + "/" + kMenuRbfName;
    (void)menu_path_.assign(menu);
    if (!args_.movie.empty()) {
        if (auto s = cores::tas::movie_system_for(w_.vfs, args_.movie.view()))
            sys_ = *s;
    }
    if (core_path_.empty()
        && cores::tas::movie_system_supported(sys_)
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
    return id->conf_name.view() == sys_.conf_str_name;
}

bool TastySession::video_reloaded_() const noexcept {
    return !w_.video || w_.video->stats().edges != video_edges_;
}

bool TastySession::menu_ready_() const noexcept {
    if (id_gen_.held() && w_.identity.generation() == id_gen_)
        return false;
    const std::optional<app::SessionIdentity> id = w_.identity.copy();
    if (!id)
        return false;
    return id->policy().front_end;
}

bool TastySession::ask_core_(const app::PathText& path) noexcept {
    if (path.empty())
        return true;
    id_gen_ = w_.identity.generation();
    if (w_.video)
        video_edges_ = w_.video->stats().edges;
    app::UiRequest::LoadCore req{};
    req.xml = app::XmlKind::Rbf;
    req.path = path;
    return w_.asks.push(req) != proto::kUncaused;
}

bool TastySession::copy_seed_() noexcept {
    if (!args_.save)
        return false;
    auto bytes = app::read_file(w_.vfs, args_.save->view(), kSeedMax);
    if (!bytes)
        return false;
    if (!card_.ensure_dir(kTastySaveRoot))
        return false;
    std::string seed(kTastySaveRoot);
    seed += '/';
    seed.append(app::SessionOwner::kReplaySeedName);
    return card_.replace_file(seed, std::as_bytes(std::span(*bytes))).has_value();
}

void TastySession::fail_(const char* why) noexcept {
    char buf[1500];
    tasty_say(tasty_render(buf, "tasty: {}", std::string_view{why != nullptr ? why : ""}));
    w_.run_exit.publish(mister::fw::refused());
    go_menu_();
}

void TastySession::fail_refusal_() noexcept {
    const std::optional<ReplayRefusal> refusal = w_.replay.refusal();
    if (!refusal) {
        fail_("replay refused");
        return;
    }
    char buf[1400];
    const char* sentence = tasty_refusal_sentence(*refusal, args_.movie.view(), args_.strict, buf);
    fail_(sentence != nullptr ? sentence : "replay refused");
}

TastySession::Stage TastySession::after_hide_() const noexcept {
    if (!args_.no_splash)
        return Stage::SplashMap;
    return after_splash_();
}

TastySession::Stage TastySession::after_splash_() const noexcept {
    return run_.loads_core() ? Stage::LoadCore : Stage::Start;
}

app::OutputSize TastySession::splash_size_() const noexcept {
    const app::OutputSize out = w_.video ? w_.video->output_size() : app::OutputSize{};
    return out.or_default(
        app::OutputSize{
            app::OutputPixels::of(TastySplash::kDefaultW),
            app::OutputPixels::of(TastySplash::kDefaultH)
        }
    );
}

std::optional<std::uint8_t> TastySession::direct_video_resolved_() const noexcept {
    if (w_.video && !w_.video->ini_read())
        return std::nullopt;
    if (!w_.direct_video_ini.generation().held())
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
    const IReplayPort::SettingNo n = w_.replay.settings_set();
    for (; !settings_printed_.reached(n); settings_printed_ = settings_printed_.next()) {
        const std::string line = w_.replay.setting_line(settings_printed_);
        char buf[1500];
        tasty_say(tasty_render(buf, "tasty: {}", std::string_view{line}));
    }
}

void TastySession::go_menu_() noexcept {
    stage_ = Stage::LoadMenu;
    stage_at_ = {};
}

bool TastySession::rec_only_over_() noexcept {
    app::capture::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return false;

    if (rec.state != app::capture::RecState::Idle) {
        if (rec.state != app::capture::RecState::Probing)
            rec_seen_ = true;
        return false;
    }
    const app::capture::RecVerdict verdict = w_.rec.last_verdict();
    if (verdict == app::capture::RecVerdict::Started)
        rec_seen_ = true;
    const bool recording_refused = verdict != app::capture::RecVerdict::None
        && verdict != app::capture::RecVerdict::Started;
    if (recording_refused && !rec_seen_) {
        char buf[160];
        tasty_say(tasty_render(
            buf,
            "tasty: the recording did not start ({})",
            app::capture::rec_verdict_name(verdict)
        ));
        w_.run_exit.publish(refused());
    }
    note_rec_failure_(rec);
    return rec_seen_ || recording_refused;
}

void TastySession::sample_rec_failure_() noexcept {
    if (!args_.record || args_.record->empty())
        return;
    app::capture::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return;
    if (rec.state != app::capture::RecState::Idle && rec.state != app::capture::RecState::Probing)
        rec_seen_ = true;
    note_rec_failure_(rec);
}

void TastySession::note_rec_failure_(const app::capture::RecorderStatus& rec) noexcept {
    if (rec_failure_said_ || !rec_seen_)
        return;
    if (rec.state != app::capture::RecState::Idle)
        return;
    if (rec.end != app::capture::RecVerdict::WriterFailed
        && rec.end != app::capture::RecVerdict::NoMemory)
        return;
    rec_failure_said_ = true;
    char buf[192];
    tasty_say(tasty_render(
        buf,
        "tasty: the recording failed after it started ({}): the AVI may be incomplete",
        app::capture::rec_verdict_name(rec.end)
    ));
    w_.run_exit.publish(refused());
}

namespace {

void say_scale_step(const app::capture::EncodeStatus::Video& v) noexcept {
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
    app::capture::RecorderStatus rec{};
    if (w_.recstat.sample_into(rec) == 0)
        return false;
    return rec.end == app::capture::RecVerdict::ScalerPortStuck;
}

void TastySession::write_status_(bool force) noexcept {

    static_assert(status_line_fits<StatusText>(), "the widest status line must fit its one buffer");
    const os::RawMonotonicClock::time_point now = clock_.now();
    if (!force && last_status_at_ && now - *last_status_at_ < kStatusPeriod)
        return;
    app::ReplayStatus play{};
    app::capture::RecorderStatus rec{};
    (void)w_.play.sample_into(play);
    (void)w_.recstat.sample_into(rec);
    app::capture::EncodeStatus enc{};
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
    if (returned && enc.video.scale == app::capture::PictureScale::Half)
        say_scale_return();
    if (stepped)
        say_scale_step(enc.video);
    if (returned && enc.video.scale != app::capture::PictureScale::Half)
        say_scale_return();
    const app::MovieFrameCount total = w_.replay.movie_frames();
    const char* end = replay_refused_() ? "refused" : app::replay_end_name(play.end);
    const proto::FieldRep video_scale = proto::printed_field(
        app::capture::picture_scale_digit(enc.video.scale)
    );
    const char* const level = app::replay_level_name(play.level);
    char text[StatusText::kBufSize];
    const int n = std::snprintf(
        text,
        sizeof text,
        kStatusLineFormat,
        level,
        end,
        play.movie_frame.value_or(app::kNoMovieFrame).text().c_str(),
        total.text().c_str(),
        app::capture::rec_state_name(rec.state),
        rec.rows.text().c_str(),
        play.late,
        play.underruns,
        play.skipped_edges.text().c_str(),
        play.pad_drops,
        rec.missed.text().c_str(),
        video_scale
    );

    if (n <= 0 || std::cmp_greater_equal(n, StatusText::kBufSize))
        return;
    const std::string_view line{text, static_cast<std::size_t>(n)};
    if (!force && last_status_.view() == line) {
        last_status_at_ = now;
        return;
    }
    if (!w_.status.write(line))
        return;

    if (!last_status_.assign(line))
        return;
    last_status_at_ = now;
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
        const app::capture::RecMode mode = args_.hashes_only ? app::capture::RecMode::Hash
                                                             : app::capture::RecMode::Avi;
        if (run_.arms_recording()) {
            (void)w_.rec.take_arm(rec, mode, args_.rec);
        } else {
            (void)w_.rec.take_start(rec, mode, args_.rec);
        }
    }
    if (run_.replays()) {
        const bool seeded = args_.save.has_value();
        if (seeded && !copy_seed_()) {
            fail_("replay refused: cannot copy the --save file");
            return false;
        }
        const app::replay::ReplayFeeder::Play play{
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
        fail_(app::capture::rec_verdict_remedy(app::capture::RecVerdict::ScalerPortStuck));
        return false;
    }
    if (run_.ends_with_recording()) {
        if (rec_only_over_())
            go_menu_();
        return false;
    }

    sample_rec_failure_();
    if (replay_refused_()) {
        fail_refusal_();
        return false;
    }
    return true;
}

void TastySession::on_replay_end_(
    app::ReplayEnd end,
    app::EpochFail epoch_fail,
    os::RawMonotonicClock::time_point now
) noexcept {
    if (end == app::ReplayEnd::None || end == app::ReplayEnd::kCount)
        return;
    if (end == app::ReplayEnd::EpochAmbiguous && w_.replay.active())
        return;
    if (end == app::ReplayEnd::Finished && args_.loop) {
        if (w_.replay.active())
            return;
        settings_printed_ = IReplayPort::SettingNo{};
        direct_video_line_ = false;
        stage_ = Stage::Start;
        return;
    }
    if (end != app::ReplayEnd::Finished && end != app::ReplayEnd::Stopped) {
        char buf[800];
        tasty_say(tasty_render(buf, "tasty: {}", app::replay_end_sentence(end, epoch_fail)));
        w_.run_exit.publish(refused());
    }
    if (args_.stay && end == app::ReplayEnd::Finished)
        return;
    if (end != app::ReplayEnd::Finished) {
        go_menu_();
    } else {
        stage_ = Stage::ReturnWait;
        stage_at_ = now;
    }
}

void TastySession::serve() noexcept {
    TASTY_SEAT_BODY(TastySession);
    if (stage_ == Stage::HideOsd) {

        const bool sent = w_.link_tx.push(
            proto::LinkOp::SetOsdVisible{.show = proto::LinkOp::OsdShow::Off}
        );
        if (sent || ++hide_tries_ >= kHideTries)
            stage_ = after_hide_();
    }
    if (!w_.video && stage_ >= Stage::SplashMap && stage_ <= Stage::SplashHide) {
        stage_ = after_splash_();
    }
    const bool stop = w_.stop.load(std::memory_order_relaxed) != 0;
    if (stop && stage_ != Stage::LoadMenu && stage_ != Stage::WaitMenu && stage_ != Stage::Done) {
        go_menu_();
    }
    const os::RawMonotonicClock::time_point now = clock_.now();
    switch (stage_) {
        case Stage::HideOsd:
            break;
        case Stage::SplashMap:
            stage_ = w_.video_fb && static_cast<bool>(splash_.map(*w_.video_fb)) ? Stage::SplashBlit
                                                                                 : after_splash_();
            break;
        case Stage::SplashBlit:
            stage_ = splash_.blit(splash_size_()) ? Stage::SplashShow : after_splash_();
            break;
        case Stage::SplashShow:
            if (!splash_.show(*w_.video, splash_size_())) {
                stage_ = after_splash_();
                break;
            }
            stage_ = Stage::SplashWait;
            stage_at_ = now;
            break;
        case Stage::SplashWait:
            if (now - stage_at_ >= TastySplash::kShowNs)
                stage_ = Stage::SplashHide;
            break;
        case Stage::SplashHide:
            (void)splash_.hide(*w_.video);
            stage_ = after_splash_();
            break;
        case Stage::LoadCore:
            if (core_path_.empty()) {
                fail_("no core to load");
                break;
            }
            ask_owed_ = !ask_core_(core_path_);
            stage_ = Stage::WaitId;
            stage_at_ = now;
            break;
        case Stage::WaitId:
            if (ask_owed_)
                ask_owed_ = !ask_core_(core_path_);
            if (core_ready_() && video_reloaded_()) {
                stage_ = Stage::Start;
            } else if (now - stage_at_ > kCoreLoadTimeout) {
                fail_(ask_owed_ ? "core load request refused" : "core load timed out");
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
                && play.movie_frame.value_or(app::kNoMovieFrame) >= kResampleFrame) {
                w_.video->resample_geometry();
                resampled_ = true;
            }
            on_replay_end_(play.end, play.epoch_fail, now);
            break;
        }
        case Stage::ReturnWait:
            sample_rec_failure_();
            write_status_();
            if (now - stage_at_ >= args_.linger) {
                go_menu_();
            }
            break;
        case Stage::LoadMenu:
            sample_rec_failure_();
            w_.replay.take_stop();
            (void)w_.rec.take_stop();
            ask_owed_ = !ask_core_(menu_path_);
            stage_ = Stage::WaitMenu;
            stage_at_ = now;
            break;
        case Stage::WaitMenu:
            if (ask_owed_)
                ask_owed_ = !ask_core_(menu_path_);
            if (menu_ready_() || now - stage_at_ > kMenuLoadTimeout) {
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
