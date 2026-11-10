// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hash_stage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cores/rom_digest.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

using Refusal = PlayRefusal::Why;

IReplayStage::Stage HashStage::level() const noexcept {
    TASTY_SEAT_BODY(HashStage);
    return Stage::Hashing;
}

IReplayStage::Step HashStage::stop(Ctx) noexcept {
    TASTY_SEAT_BODY(HashStage);
    return Step::done();
}

IReplayStage::Step HashStage::enter(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(HashStage);
    const ReplayPlay::Request& req = ctx.request;
    const ReplayPlay::ScannedMovie& scan = ctx.scan;

    const auto src = req.codec->format().digest_source(*ctx.w.vfs, req.rom.view());
    if (!src) {

        using CR = cores::MovieRefusal;
        if (src.error().code == Errc::bad_format) {
            const auto why = cores::movie_refusal_of(src.error());
            return Step::refuse(
                why == CR::Disk                 ? Refusal::RomKind
                    : why == CR::ChdUnsupported ? Refusal::RomChd
                                                : Refusal::RomSize,
                src.error().detail
            );
        }
        return Step::refuse(Refusal::RomIo, static_cast<std::uint32_t>(src.error().code));
    }
    auto rom = ctx.w.vfs->open(src->file, svc::OpenMode::Read);
    if (!rom)
        return Step::refuse(Refusal::RomIo, static_cast<std::uint32_t>(rom.error().code));

    const std::optional<cores::DigestValue>& header_digest = scan.facts.digest;
    const cores::IMovieFormat::DigestSpan span{
        .offset = src->span.offset,
        .length = header_digest ? src->span.length : proto::FileSize{}
    };
    cores::RomDigest digest = header_digest ? cores::RomDigest{header_digest->kind}
                                            : cores::RomDigest{};
    if (header_digest)
        digest.update(src->prefix);
    std::vector<cores::RomDigest> alts{};
    if (header_digest) {
        for (const auto& prefix : src->alt_prefixes) {
            alts.emplace_back(header_digest->kind).update(prefix);
        }
    }
    hasher_.arm(std::move(*rom), span, std::move(digest), std::move(alts));
    firmware_pass_ = false;
    return Step::stay();
}

IReplayStage::Step HashStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(HashStage);
    switch (hasher_.step()) {
        case RomHasher::Pace::Pending:
            return Step::stay();
        case RomHasher::Pace::ReadFailed:
            return Step::refuse(firmware_pass_ ? Refusal::Firmware : Refusal::RomIo);
        case RomHasher::Pace::Done:
            break;
    }
    return finish_rom_(ctx);
}

IReplayStage::Step HashStage::finish_rom_(Ctx ctx) noexcept {
    const ReplayPlay::Request& req = ctx.request;
    const ReplayPlay::ScannedMovie& scan = ctx.scan;
    ReplayPlay::Report& report = ctx.report;
    if (firmware_pass_) {
        if (hasher_.digest().finish() != *scan.facts.firmware)
            return Step::refuse(Refusal::Firmware, 1);
        report.refusal_path.clear();
        return Step::enter(Step::Next::Settle);
    }
    bool alt = false;
    for (auto& a : hasher_.alts())
        alt = alt || req.codec->format().rom_matches(a.finish(), scan.facts);
    hasher_.alts().clear();
    if (!req.codec->format().rom_matches(hasher_.digest().finish(), scan.facts) && !alt)
        return Step::refuse(Refusal::Checksum);
    const auto c = scan.facts.firmware ? req.codec->core().companion(req.rom.view(), scan.facts)
                                       : std::nullopt;
    if (scan.facts.firmware && !c)
        return Step::refuse(Refusal::Firmware);
    if (!c)
        return Step::enter(Step::Next::Settle);

    for (const std::string& path : c->paths) {
        if (path.empty())
            continue;
        if (report.refusal_path.empty())
            report.refusal_path = path;
        auto f = ctx.w.vfs->open(path, svc::OpenMode::Read);
        if (!f)
            continue;
        const auto size = (*f)->size();
        if (!size || *size != c->size)
            continue;
        hasher_.arm(
            std::move(*f),
            cores::span_past(*size, proto::FileSize{}),
            cores::RomDigest{scan.facts.firmware->kind}
        );
        firmware_pass_ = true;
        report.refusal_path = path;
        return Step::stay();
    }
    return Step::refuse(Refusal::Firmware);
}

}
