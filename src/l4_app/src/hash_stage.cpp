// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hash_stage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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
    ReplayPlay& play = ctx.play;

    const auto src = play.codec->digest_source(*ctx.w.vfs, play.rom.view());
    if (!src) {

        using CR = cores::IMovieCodec::Refusal;
        const auto why = static_cast<CR>(src.error().detail);
        if (src.error().code == Errc::bad_format)
            return Step::refuse(
                why == CR::Disk                 ? Refusal::RomKind
                    : why == CR::ChdUnsupported ? Refusal::RomChd
                                                : Refusal::RomSize,
                src.error().detail
            );
        return Step::refuse(Refusal::RomIo, static_cast<std::uint32_t>(src.error().code));
    }
    auto rom = ctx.w.vfs->open(src->file, svc::OpenMode::Read);
    if (!rom)
        return Step::refuse(Refusal::RomIo, static_cast<std::uint32_t>(rom.error().code));

    const std::uint64_t rom_off = src->span.offset;
    const std::uint64_t rom_end = play.facts.has_digest ? src->span.offset + src->span.length
                                                        : rom_off;
    cores::RomDigest digest{play.facts.digest.kind};
    if (play.facts.has_digest)
        digest.update(src->prefix);
    std::optional<cores::RomDigest> alt{};
    if (play.facts.has_digest && src->alt_prefix.has_value()) {
        alt.emplace(play.facts.digest.kind);
        alt->update(*src->alt_prefix);
    }
    hasher_.arm(std::move(*rom), rom_off, rom_end, std::move(digest), std::move(alt));
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
    ReplayPlay& play = ctx.play;
    if (firmware_pass_) {
        if (hasher_.digest().finish() != play.facts.firmware)
            return Step::refuse(Refusal::Firmware, 1);
        play.refusal_path.clear();
        return Step::enter(Step::Next::Settle);
    }
    const bool alt = hasher_.alt() && play.codec->rom_matches(hasher_.alt()->finish(), play.facts);
    hasher_.alt().reset();
    if (!play.codec->rom_matches(hasher_.digest().finish(), play.facts) && !alt)
        return Step::refuse(Refusal::Checksum);
    const auto c = play.facts.has_firmware ? play.codec->companion(play.rom.view(), play.facts)
                                           : std::nullopt;
    if (play.facts.has_firmware && !c)
        return Step::refuse(Refusal::Firmware);
    if (!c)
        return Step::enter(Step::Next::Settle);

    for (const std::string& path : c->paths) {
        if (path.empty())
            continue;
        if (play.refusal_path.empty())
            play.refusal_path = path;
        auto f = ctx.w.vfs->open(path, svc::OpenMode::Read);
        if (!f)
            continue;
        const auto size = (*f)->size();
        if (!size || size->v != c->size)
            continue;
        hasher_.arm(std::move(*f), 0, size->v, cores::RomDigest{play.facts.firmware.kind});
        firmware_pass_ = true;
        play.refusal_path = path;
        return Step::stay();
    }
    return Step::refuse(Refusal::Firmware);
}

}
