// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_session.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "app/rbf_resolve.h"
#include "app/replay_status.h"
#include "app/recorder_control.h"
#include "app/recorder_status.h"
#include "app/session_identity.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/xml_kind.h"
#include "app/video_pump.h"
#include "app/identity_latch.h"
#include "app/link_tx_channel.h"
#include "cores/registry.h"
#include "hub.h"
#include "proto/link_op.h"
#include "svc/vfs.h"
#include "tasty_registry.h"
#include "tasty_ctl.h"

namespace mister::fw {
namespace {
constexpr std::int64_t kStatusPeriodNs = 250'000'000;

constexpr unsigned kHideTries = 50;

constexpr std::int32_t kResampleFrame = 60;
}  // namespace

TastySession::TastySession(const Wiring& w, const TastyArgs& args) noexcept : w_(w), args_(args) {
    rec_only_ = args_.verb == TastyVerb::RecStart;
    if (!args_.core.empty()) (void)core_path_.assign(args_.core.view());
    if (w_.vfs != nullptr) {
        std::string menu = std::string(w_.vfs->root_path()) + "/" + kMenuRbfName;
        (void)menu_path_.assign(menu);
        if (!args_.movie.empty()) {
            if (auto s = cores::movie_system_for(*w_.vfs, args_.movie.view())) sys_ = *s;
        }
        if (core_path_.empty() && cores::movie_system_supported(sys_) &&
            tasty_plays(sys_.conf_str_name)) {
            const std::string dir = std::string(w_.vfs->root_path()) + "/_Console";
            if (auto n = app::resolve_rbf_name(*w_.vfs, dir, sys_.conf_str_name, false); n) {
                const std::string p = dir + "/" + *n;
                (void)core_path_.assign(p);
            }
        }
    }
}

bool TastySession::core_ready_() const noexcept {
    if (w_.identity == nullptr) return false;
    if (w_.identity->generation() == id_gen_) return false;
    app::SessionIdentity id{};
    if (!w_.identity->copy(id)) return false;
    if (sys_.conf_str_name.empty()) return true;
    return std::string_view{id.rbf} == sys_.conf_str_name;
}

bool TastySession::video_reloaded_() const noexcept {
    return w_.video == nullptr || w_.video->stats().edges != video_edges_;
}

bool TastySession::menu_ready_() const noexcept {
    if (w_.identity == nullptr) return false;
    if (id_gen_ != 0 && w_.identity->generation() == id_gen_) return false;
    app::SessionIdentity id{};
    if (!w_.identity->copy(id)) return false;
    return cores::profile_for(id.rbf).is_front_end;
}

void TastySession::ask_core_(std::string_view path) noexcept {
    if (w_.asks == nullptr || path.empty()) return;
    if (w_.identity != nullptr) id_gen_ = w_.identity->generation();
    if (w_.video != nullptr) video_edges_ = w_.video->stats().edges;
    app::UiRequest::LoadCore req{};
    req.xml = app::XmlKind::Rbf;
    if (!req.path.assign(path)) return;
    (void)w_.asks->push(req);
}

void TastySession::fail_(int rc, const char* why) noexcept {
    std::fprintf(stderr, "tasty: %s\n", why);
    if (w_.exit_code != nullptr) w_.exit_code->store(rc, std::memory_order_relaxed);
    go_menu_();
}

void TastySession::fail_refusal_() noexcept {
    const char* why = w_.replay != nullptr ? w_.replay->refusal_why() : "none";
    const std::string_view set = w_.replay != nullptr ? w_.replay->refusal_setting() : "";
    const std::string_view path = w_.replay != nullptr ? w_.replay->refusal_path() : "";
    const bool offered = w_.replay != nullptr && w_.replay->refusal_setting_offered();
    char buf[1400];
    if (std::strcmp(why, "setting") == 0 && offered && args_.strict) {
        std::snprintf(buf, sizeof buf,
                      "replay refused (--strict): the core's %.*s is not the movie's; without "
                      "--strict tasty sets it for this run",
                      static_cast<int>(set.size()), set.data());
    } else if (std::strcmp(why, "setting") == 0 && offered) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: tasty could not set the core's %.*s for this run",
                      static_cast<int>(set.size()), set.data());
    } else if (std::strcmp(why, "setting") == 0) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: this movie needs a %.*s this core does not offer",
                      static_cast<int>(set.size()), set.data());
    } else if (std::strcmp(why, "firmware") == 0 && !path.empty()) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: the movie was recorded with another BIOS; it needs its own "
                      "at %.*s",
                      static_cast<int>(path.size()), path.data());
    } else if (std::strcmp(why, "firmware") == 0) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: the movie was recorded with a BIOS; put it beside the disc "
                      "image");
    } else if (std::strcmp(why, "savestate") == 0) {
        const std::string_view movie = args_.movie.view();
        std::snprintf(buf, sizeof buf,
                      "replay refused: %.*s starts from a savestate or saved game, not from "
                      "power-on",
                      static_cast<int>(movie.size()), movie.data());
    } else if (std::strcmp(why, "rom_kind") == 0) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: this disc image cannot be hashed; use a Redump .cue");
    } else if (std::strcmp(why, "checksum") == 0) {
        std::snprintf(buf, sizeof buf,
                      "replay refused: the ROM on the card is not the one the movie was recorded "
                      "with");
    } else if (set.empty()) {
        std::snprintf(buf, sizeof buf, "replay refused: %s", why);
    } else {
        std::snprintf(buf, sizeof buf, "replay refused: %s %.*s", why, static_cast<int>(set.size()),
                      set.data());
    }
    fail_(1, buf);
}

TastySession::Stage TastySession::after_hide_() const noexcept {
    if (!args_.no_splash) return Stage::SplashMap;
    return rec_only_ ? Stage::Start : Stage::LoadCore;
}

void TastySession::print_settings_() noexcept {
    if (w_.replay == nullptr) return;
    const std::size_t n = w_.replay->settings_set();
    for (; settings_printed_ < n; ++settings_printed_) {
        const std::string line = w_.replay->setting_line(settings_printed_);
        std::fprintf(stderr, "tasty: %s\n", line.c_str());
    }
}

void TastySession::go_menu_() noexcept {
    stage_ = Stage::LoadMenu;
    stage_ns_ = 0;
}

void TastySession::write_status_(bool force) noexcept {
    if (w_.play == nullptr || w_.recstat == nullptr) return;
    const std::int64_t now = clock_.now().count();
    if (!force && last_status_ns_ != 0 && now - last_status_ns_ < kStatusPeriodNs) return;
    app::ReplayStatus play{};
    app::RecorderStatus rec{};
    (void)w_.play->sample_into(play);
    (void)w_.recstat->sample_into(rec);
    const std::uint32_t total = w_.replay != nullptr ? w_.replay->movie_frames() : 0;
    const char* end =
        (w_.replay != nullptr && w_.replay->refused()) ? "refused" : app::replay_end_name(play.end);
    char buf[768];
    const int n =
        std::snprintf(buf, sizeof buf,
                      "{\"play\":\"%s\",\"end\":\"%s\",\"frame\":%d,\"frames\":%u,\"rec\":\"%s\","
                      "\"rows\":%u,\"late\":%u,\"underruns\":%u,\"skipped\":%u,\"drops\":%u,"
                      "\"missed\":%u}\n",
                      play.level == app::ReplayLevel::Running        ? "running"
                      : play.level == app::ReplayLevel::AwaitPowerOn ? "arming"
                                                                     : "idle",
                      end, play.movie_frame, total, app::rec_state_name(rec.state), rec.rows,
                      play.late, play.underruns, play.skipped_edges, play.pad_drops, rec.missed);
    if (n <= 0) return;
    const unsigned un = static_cast<unsigned>(n);
    if (!force && un == last_status_n_ && std::memcmp(last_status_, buf, un) == 0) {
        last_status_ns_ = now;
        return;
    }
    if (tasty_write_status(std::string_view(buf, un))) {
        last_status_ns_ = now;
        last_status_n_ = un;
        std::memcpy(last_status_, buf, un);
    }
}

void TastySession::tick() noexcept {
    TASTY_SEAT_BODY(TastySession);
    if (stage_ == Stage::HideOsd) {

        const bool sent =
            w_.link_tx == nullptr ||
            w_.link_tx->push(proto::LinkOp::SetOsdVisible{.show = proto::LinkOp::OsdShow::Off});
        if (sent || ++hide_tries_ >= kHideTries) stage_ = after_hide_();
    }
    if (w_.video == nullptr && stage_ >= Stage::SplashMap && stage_ <= Stage::SplashHide) {
        stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
    }
    const bool stop = w_.stop != nullptr && w_.stop->load(std::memory_order_relaxed) != 0;
    if (stop && stage_ != Stage::LoadMenu && stage_ != Stage::WaitMenu && stage_ != Stage::Done) {
        go_menu_();
    }
    const std::int64_t now = clock_.now().count();
    const std::uint32_t gw = w_.video != nullptr && w_.video->output_width() != 0
                                 ? w_.video->output_width()
                                 : TastySplash::kDefaultW;
    const std::uint32_t gh = w_.video != nullptr && w_.video->output_height() != 0
                                 ? w_.video->output_height()
                                 : TastySplash::kDefaultH;

    switch (stage_) {
        case Stage::HideOsd:
            break;
        case Stage::SplashMap:
            splash_ok_ = static_cast<bool>(splash_.map(w_.video_fb));
            stage_ = splash_ok_ ? Stage::SplashBlit : (rec_only_ ? Stage::Start : Stage::LoadCore);
            break;
        case Stage::SplashBlit:
            splash_ok_ = splash_.blit(gw, gh);
            stage_ = splash_ok_ ? Stage::SplashShow : (rec_only_ ? Stage::Start : Stage::LoadCore);
            break;
        case Stage::SplashShow:
            if (!splash_.show(*w_.video, gw, gh)) {
                stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
                break;
            }
            stage_ = Stage::SplashWait;
            stage_ns_ = now;
            break;
        case Stage::SplashWait:
            if (now - stage_ns_ >= TastySplash::kShowNs) stage_ = Stage::SplashHide;
            break;
        case Stage::SplashHide:
            (void)splash_.hide(*w_.video);
            stage_ = rec_only_ ? Stage::Start : Stage::LoadCore;
            break;
        case Stage::LoadCore:
            if (core_path_.empty()) {
                fail_(1, "no core to load");
                break;
            }
            ask_core_(core_path_.view());
            stage_ = Stage::WaitId;
            stage_ns_ = now;
            break;
        case Stage::WaitId:
            if (core_ready_() && video_reloaded_()) {
                stage_ = Stage::Start;
            } else if (now - stage_ns_ > 30'000'000'000) {
                fail_(1, "core load timed out");
            }
            break;
        case Stage::Start: {
            const app::RecMode mode = args_.hashes_only ? app::RecMode::Hash : app::RecMode::Avi;
            if (!args_.record.empty()) {
                if (args_.verb == TastyVerb::Play) {
                    if (w_.rec != nullptr) (void)w_.rec->take_arm(args_.record.view(), mode);
                } else {
                    if (w_.rec != nullptr) (void)w_.rec->take_start(args_.record.view(), mode);
                }
            }
            if (!rec_only_) {
                const PlayAsk ask{.movie = args_.movie.view(),
                                  .rom = args_.rom.view(),
                                  .lead = args_.lead,
                                  .stop_at = args_.stop_at,
                                  .set_settings = !args_.strict};
                if (w_.replay == nullptr || !w_.replay->take_play(ask)) {
                    fail_refusal_();
                    break;
                }
                play_started_ = true;
            }
            stage_ = Stage::Run;
            break;
        }
        case Stage::Run: {
            print_settings_();
            write_status_();
            if (rec_only_) break;
            if (w_.replay != nullptr && w_.replay->refused()) {
                fail_refusal_();
                break;
            }
            app::ReplayStatus play{};
            if (w_.play != nullptr) (void)w_.play->sample_into(play);

            if (!resampled_ && !args_.record.empty() && w_.video != nullptr &&
                play.level == app::ReplayLevel::Running && play.movie_frame >= kResampleFrame) {
                w_.video->resample_geometry();
                resampled_ = true;
            }
            const auto end = play.end;
            if (end == app::ReplayEnd::None || end == app::ReplayEnd::kCount) break;
            if (end == app::ReplayEnd::EpochAmbiguous && w_.replay != nullptr &&
                w_.replay->active())
                break;
            if (end != app::ReplayEnd::Finished && end != app::ReplayEnd::Stopped) {
                std::fprintf(stderr, "tasty: replay ended %s\n", app::replay_end_name(end));
                if (w_.exit_code != nullptr) w_.exit_code->store(1, std::memory_order_relaxed);
            }
            if (args_.stay && end == app::ReplayEnd::Finished) break;
            if (end != app::ReplayEnd::Finished) {
                go_menu_();
            } else {
                stage_ = Stage::ReturnWait;
                stage_ns_ = now;
            }
            break;
        }
        case Stage::ReturnWait:
            write_status_();
            if (now - stage_ns_ >=
                static_cast<std::int64_t>(args_.return_after_s) * 1'000'000'000) {
                go_menu_();
            }
            break;
        case Stage::LoadMenu:
            if (w_.replay != nullptr) (void)w_.replay->take_stop();
            if (w_.rec != nullptr) (void)w_.rec->take_stop();
            if (!menu_path_.empty()) ask_core_(menu_path_.view());
            stage_ = Stage::WaitMenu;
            stage_ns_ = now;
            break;
        case Stage::WaitMenu:
            if (menu_ready_() || now - stage_ns_ > 20'000'000'000) {
                write_status_(true);
                want_stock_ = true;
                stage_ = Stage::Done;
            }
            break;
        case Stage::Done:
            if (want_stock_ && w_.want_stock != nullptr)
                w_.want_stock->store(1, std::memory_order_relaxed);
            if (!stop_sent_ && w_.main_stop != nullptr) w_.main_stop->request();
            stop_sent_ = true;
            break;
    }
}

}  // namespace mister::fw
