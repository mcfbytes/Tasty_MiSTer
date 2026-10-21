// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scan_stage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include "app/replay_msg.h"
#include "app/video_pump.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

using Refusal = PlayRefusal::Why;

IReplayStage::Stage ScanStage::level() const noexcept {
    TASTY_SEAT_BODY(ScanStage);
    return Stage::Scanning;
}

IReplayStage::Step ScanStage::stop(Ctx) noexcept {
    TASTY_SEAT_BODY(ScanStage);
    return Step::done();
}

IReplayStage::Step ScanStage::enter(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(ScanStage);
    ReplayPlay& play = ctx.play;
    auto f = play.codec->open_movie(*ctx.w.vfs, play.movie.view());
    if (!f) {

        if (play.codec->archive() != nullptr && f.error().code == Errc::bad_format)
            return Step::refuse(Refusal::Movie, f.error().detail);
        return Step::refuse(Refusal::MovieIo, static_cast<std::uint32_t>(f.error().code));
    }
    const auto size = (*f)->size();
    if (!size)
        return Step::refuse(Refusal::MovieIo, static_cast<std::uint32_t>(size.error().code));
    file_ = std::move(*f);
    file_size_ = size->v;
    play.runs.clear();
    off_ = 0;
    line_ = Line{};
    play.facts = {};
    in_log_ = false;
    log_done_ = false;
    play.frames = 0;
    play.movie_frames = 0;
    play.tries = 0;
    play.vsync_ok = ctx.w.video.output_locked();
    return Step::stay();
}

IReplayStage::Step ScanStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(ScanStage);
    return read_lines_(ctx);
}

IReplayStage::Step ScanStage::read_lines_(Ctx ctx) noexcept {
    ReplayPlay& play = ctx.play;
    std::array<std::byte, ReplayPlay::kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < ReplayPlay::kTickBudget) {
        if (off_ >= file_size_) {
            if (line_.len != 0 || line_.clipped) {
                const Line l = line_;
                line_ = Line{};
                if (const auto r = on_line_(ctx, std::string_view(l.text.data(), l.len), l.clipped))
                    return Step::refuse(r->why, r->detail);
            }
            return finish_scan_(ctx);
        }
        const auto got = file_->read_at(off_, buf);
        if (!got || *got == 0) {
            if (!got && got.error().code == Errc::bad_format && play.codec->archive() != nullptr)
                return Step::refuse(Refusal::Movie, got.error().detail);
            return Step::refuse(
                Refusal::MovieIo,
                got ? 0u : static_cast<std::uint32_t>(got.error().code)
            );
        }
        spent += static_cast<std::uint32_t>(*got);
        for (std::size_t i = 0; i < *got; ++i) {
            const char c = static_cast<char>(buf[i]);
            if (c == '\n') {
                const Line l = line_;
                line_ = Line{};
                if (const auto r = on_line_(ctx, std::string_view(l.text.data(), l.len), l.clipped))
                    return Step::refuse(r->why, r->detail);
                continue;
            }
            if (line_.len < line_.text.size()) {
                line_.text[line_.len++] = c;
            } else {
                line_.clipped = true;
            }
        }
        off_ += *got;
    }
    return Step::stay();
}

std::optional<PlayRefusal> ScanStage::on_line_(
    Ctx ctx,
    std::string_view line,
    bool clipped
) noexcept {
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.empty() && !clipped)
        return std::nullopt;
    if (in_log_ && (log_done_ || ctx.play.codec->ends_log(line))) {
        log_done_ = true;
        return std::nullopt;
    }
    return scan_line_(ctx, line, clipped);
}

std::optional<PlayRefusal> ScanStage::scan_line_(
    Ctx ctx,
    std::string_view line,
    bool clipped
) noexcept {
    using CR = cores::IMovieCodec::Refusal;
    ReplayPlay& play = ctx.play;
    if (!in_log_) {
        if (!play.codec->starts_log(line)) {
            const auto f = play.codec->header_line(line, play.facts);
            if (!f)
                return PlayRefusal{Refusal::Movie, f.error().detail};
            play.facts = *f;
            return std::nullopt;
        }
        if (const auto e = play.codec->finish_header(play.facts); !e)
            return PlayRefusal{Refusal::Movie, e.error().detail};
        in_log_ = true;
    }
    if (clipped)
        return PlayRefusal{Refusal::Movie, static_cast<std::uint32_t>(CR::BadLine)};
    const auto fr = play.codec->frame(line, play.facts);
    if (!fr)
        return PlayRefusal{Refusal::Movie, fr.error().detail};

    if (play.frames > 0 && fr->commands != 0)
        return PlayRefusal{Refusal::Command, play.frames};
    if (++play.frames > ReplayPlay::kMaxFrames)
        return PlayRefusal{Refusal::TooLong, play.frames};
    if (const std::uint32_t f = play.frames - 1; !play.stop_at || f < *play.stop_at)
        note_frame_(play, f, *fr);
    return std::nullopt;
}

void ScanStage::note_frame_(
    ReplayPlay& play,
    std::uint32_t f,
    const cores::IMovieCodec::Frame& fr
) noexcept {
    std::array<std::uint32_t, kReplayPorts> m{};
    for (std::size_t p = 0; p < kReplayPorts && p < fr.mask.size(); ++p)
        m[p] = fr.mask[p];
    if (!play.runs.empty()) {
        ReplayMsg::Input& run = play.runs.back();
        if (run.mask == m && run.last - run.first + 1 < ReplayPlay::kRunSplit) {
            run.last = f;
            return;
        }
    }
    play.runs.push_back(ReplayMsg::Input{.first = f, .last = f, .mask = m});
}

IReplayStage::Step ScanStage::finish_scan_(Ctx ctx) noexcept {
    ReplayPlay& play = ctx.play;
    if (!in_log_ || play.frames == 0) {
        return Step::refuse(
            Refusal::Movie,
            static_cast<std::uint32_t>(cores::IMovieCodec::Refusal::NotAMovie)
        );
    }
    play.movie_frames = play.stop_at ? std::min(play.frames, *play.stop_at) : play.frames;

    file_.reset();
    play.runs.shrink_to_fit();
    const cores::IMovieCodec::Raster r = play.codec->raster(play.facts);
    if (play.offset_us == 0)
        play.offset_us = r.period_ns / 2000;
    if (!ReplayPlay::phase_fits(play.offset_us, r.period_ns))
        return Step::refuse(Refusal::Phase, play.offset_us);
    return Step::enter(Step::Next::Hash);
}

}
