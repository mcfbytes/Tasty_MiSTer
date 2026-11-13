// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scan_stage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include "app/replay_msg.h"
#include "app/video_pump.h"
#include "proto/types.h"
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
    const ReplayPlay::Request& req = ctx.request;
    ReplayPlay::ScannedMovie& scan = ctx.scan;
    auto f = req.codec->format().open_movie(*ctx.w.vfs, req.movie.view());
    if (!f) {

        if (req.codec->format().archive().has_value() && f.error().code == Errc::bad_format)
            return Step::refuse(Refusal::Movie, f.error().detail);
        return Step::refuse(Refusal::MovieIo, static_cast<std::uint32_t>(f.error().code));
    }
    const auto size = (*f)->size();
    if (!size)
        return Step::refuse(Refusal::MovieIo, static_cast<std::uint32_t>(size.error().code));
    file_ = std::move(*f);
    file_size_ = size->v;
    scan = {};
    off_ = 0;
    lines_.reset();
    in_log_ = false;
    log_done_ = false;
    scan.vsync_ok = ctx.w.video.output_locked();
    return Step::stay();
}

IReplayStage::Step ScanStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(ScanStage);
    return read_lines_(ctx);
}

IReplayStage::Step ScanStage::read_lines_(Ctx ctx) noexcept {
    std::array<std::byte, ReplayPlay::kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < ReplayPlay::kTickBudget) {
        if (off_ >= file_size_) {
            std::optional<PlayRefusal> refused;
            const bool go = lines_.finish([&](const cores::MovieLines::Line& l) {
                refused = on_line_(ctx, l);
                return !refused;
            });
            if (!go)
                return Step::refuse(refused->why, refused->detail);
            return finish_scan_(ctx);
        }
        const auto got = file_->read_at(off_, buf);
        if (!got || *got == 0) {
            if (!got
                && ctx.request.codec->format().archive().has_value()
                && got.error().code == Errc::bad_format)
                return Step::refuse(Refusal::Movie, got.error().detail);
            return Step::refuse(
                Refusal::MovieIo,
                got ? 0u : static_cast<std::uint32_t>(got.error().code)
            );
        }
        spent += static_cast<std::uint32_t>(*got);
        std::optional<PlayRefusal> refused;
        const bool go = lines_.feed(
            std::span<const std::byte>(buf.data(), *got),
            [&](const cores::MovieLines::Line& l) {
                refused = on_line_(ctx, l);
                return !refused;
            }
        );
        if (!go)
            return Step::refuse(refused->why, refused->detail);
        off_ += *got;
    }
    return Step::stay();
}

std::optional<PlayRefusal> ScanStage::on_line_(
    Ctx ctx,
    const cores::MovieLines::Line& line
) noexcept {
    if (line.text.empty() && !line.clipped)
        return std::nullopt;
    if (in_log_ && (log_done_ || ctx.request.codec->format().ends_log(line.text))) {
        log_done_ = true;
        return std::nullopt;
    }
    return scan_line_(ctx, line);
}

std::optional<PlayRefusal> ScanStage::scan_line_(
    Ctx ctx,
    const cores::MovieLines::Line& line
) noexcept {
    using CR = cores::MovieRefusal;
    const ReplayPlay::Request& req = ctx.request;
    ReplayPlay::ScannedMovie& scan = ctx.scan;
    if (!in_log_) {
        if (!req.codec->format().starts_log(line.text)) {
            const auto f = req.codec->format().header_line(line.text, scan.facts);
            if (!f)
                return PlayRefusal{Refusal::Movie, f.error().detail};
            scan.facts = *f;
            return std::nullopt;
        }
        if (const auto e = req.codec->format().finish_header(scan.facts); !e)
            return PlayRefusal{Refusal::Movie, e.error().detail};
        in_log_ = true;
    }
    if (line.clipped)
        return PlayRefusal{Refusal::Movie, static_cast<std::uint32_t>(CR::BadLine)};
    const auto fr = req.codec->format().frame(line.text, scan.facts);
    if (!fr)
        return PlayRefusal{Refusal::Movie, fr.error().detail};

    if (scan.scanned > 0 && fr->commands.has_any())
        return PlayRefusal{Refusal::Command, scan.scanned};
    if (++scan.scanned > ReplayPlay::kMaxFrames)
        return PlayRefusal{Refusal::TooLong, scan.scanned};
    if (const std::uint32_t f = scan.scanned - 1;
        !req.stop_at || movie_frame_from_wire(f) < *req.stop_at)
        note_frame_(scan, f, *fr);
    return std::nullopt;
}

void ScanStage::note_frame_(
    ReplayPlay::ScannedMovie& scan,
    std::uint32_t f,
    const cores::MovieFrame& fr
) noexcept {
    std::array<proto::JoyMask, kReplayPorts> m{};
    for (std::size_t p = 0; p < kReplayPorts && p < fr.mask.size(); ++p)
        m[p] = fr.mask[p];
    if (!scan.runs.empty()) {
        ReplayMsg::Input& run = scan.runs.back();
        if (run.mask == m && run.last - run.first + FrameCount{1} < ReplayPlay::kRunSplit) {
            run.last = movie_frame_from_wire(f);
            return;
        }
    }
    scan.runs.push_back(
        ReplayMsg::Input{
            .first = movie_frame_from_wire(f),
            .last = movie_frame_from_wire(f),
            .mask = m
        }
    );
}

IReplayStage::Step ScanStage::finish_scan_(Ctx ctx) noexcept {
    const ReplayPlay::Request& req = ctx.request;
    ReplayPlay::ScannedMovie& scan = ctx.scan;
    if (!in_log_ || scan.scanned == 0) {
        return Step::refuse(
            Refusal::Movie,
            static_cast<std::uint32_t>(cores::MovieRefusal::NotAMovie)
        );
    }
    const FrameCount scanned = movie_frame_from_wire(scan.scanned) - MovieFrame{};
    scan.movie_frames = req.stop_at ? std::min(scanned, *req.stop_at - MovieFrame{}) : scanned;

    file_.reset();
    scan.runs.shrink_to_fit();
    const cores::IReplayCore::Raster r = req.codec->core().raster(scan.facts);
    const CodecNs period = std::chrono::duration_cast<CodecNs>(r.period_ns);
    scan.offset_us = req.offset_us == ArmMicros::zero()
        ? std::chrono::duration_cast<ArmMicros>(period / 2)
        : req.offset_us;
    if (!ReplayPlay::phase_fits(scan.offset_us, period))
        return Step::refuse(Refusal::Phase, scan.offset_us.count());
    return Step::enter(Step::Next::Hash);
}

}
