// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_sentences.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "cores/movie_refusal.h"
#include "tasty_cli_refusal.h"
#include "tasty_say.h"

namespace mister::fw {

namespace {

constexpr const char* kBadArguments = "bad arguments (try tasty help)";

constexpr char kChdSentence[] = "this build of tasty cannot read .chd disc images";
constexpr char
    kPhaseSentence[] = "--phase must be at least 1000 us and leave 2000 us before the next frame";

}

void report_unhashable(
    const cores::MovieCodec& codec,
    std::string_view rom,
    const Error& e
) noexcept {
    using CR = cores::MovieRefusal;
    const int n = static_cast<int>(rom.size());
    const std::optional<CR> why = cores::movie_refusal_of(e);
    const std::string_view images = codec.core().disc_images();
    if (!why)
        std::fprintf(stderr, "tasty: cannot read rom %.*s\n", n, rom.data());
    else if (*why == CR::ChdUnsupported)
        std::fprintf(stderr, "tasty: %s: %.*s\n", kChdSentence, n, rom.data());
    else if (*why == CR::Disk && !images.empty())
        std::fprintf(
            stderr,
            "tasty: cannot hash disc image %.*s; use %.*s\n",
            n,
            rom.data(),
            static_cast<int>(images.size()),
            images.data()
        );
    else
        std::fprintf(stderr, "tasty: cannot hash rom %.*s\n", n, rom.data());
}

const char* tasty_movie_refusal_sentence(cores::MovieRefusal why) noexcept {
    using R = cores::MovieRefusal;
    switch (why) {
        case R::NotAMovie:
            return "this file is not a movie tasty can read; check that it is the movie itself "
                   "and complete";
        case R::Savestate:
            return "this movie starts from a savestate or saved game, not from power-on";
        case R::Binary:
            return "this .fm2 keeps its input in binary; save it again as text from FCEUX";
        case R::Disk:
            return "this movie is for a disk add-on such as the Famicom Disk System, which tasty "
                   "does not play";
        case R::PortType:
            return "this movie uses a controller other than the standard pad, which tasty does "
                   "not replay";
        case R::Multitap:
            return "this movie has more than two players (a Four Score or multitap), which tasty "
                   "does not replay";
        case R::Checksum:
            return "the movie's ROM checksum is missing or unreadable, so tasty cannot confirm "
                   "the ROM";
        case R::Command:
            return "this movie presses reset or power after it starts, which tasty does not "
                   "replay";
        case R::TooLong:
            return "this movie is longer than tasty can play";
        case R::BadLine:
            return "a line of this movie's input cannot be read; the file may be damaged";
        case R::Setting:
            return "this movie was recorded with an emulator setting, such as its region, that "
                   "this core cannot match";
        case R::Archive:
            return "this movie archive cannot be opened; check that the file is complete";
        case R::Subframe:
            return "this movie uses sub-frame input, which tasty does not replay";
        case R::System:
            return "this movie is for another console than the one tasty runs it on";
        case R::ChdUnsupported:
            return kChdSentence;
        case R::None:
        case R::kCount:
            return nullptr;
    }
    return nullptr;
}

const char* tasty_refusal_sentence(
    const ReplayRefusal& r,
    std::string_view movie,
    bool strict,
    std::span<char> out
) noexcept {
    using W = app::PlayRefusal::Why;
    switch (r.why) {
        case W::Setting:
            if (r.setting_offered && strict) {
                return tasty_render(
                           out,
                           "replay refused (--strict): the core's {} is not the movie's; "
                           "without --strict tasty sets it for this run",
                           r.setting
                )
                    .data();
            }
            if (r.setting_offered) {
                return tasty_render(
                           out,
                           "replay refused: tasty could not set the core's {} for this run",
                           r.setting
                )
                    .data();
            }
            return tasty_render(
                       out,
                       "replay refused: this movie needs a {} this core does not offer",
                       r.setting
            )
                .data();
        case W::Firmware:
            if (!r.path.empty()) {
                return tasty_render(
                           out,
                           "replay refused: the movie was recorded with another BIOS; it "
                           "needs its own at {}",
                           r.path
                )
                    .data();
            }
            return tasty_render(
                       out,
                       "replay refused: the movie was recorded with a BIOS; put it beside "
                       "the disc image"
            )
                .data();
        case W::Movie: {
            const cores::MovieRefusal why = r.movie.value_or(cores::MovieRefusal::None);
            if (why == cores::MovieRefusal::Savestate) {
                return tasty_render(
                           out,
                           "replay refused: {} starts from a savestate or saved game, not "
                           "from power-on",
                           movie
                )
                    .data();
            }
            const char* plain = tasty_movie_refusal_sentence(why);
            return tasty_render(
                       out,
                       "replay refused: {}",
                       std::string_view{plain != nullptr ? plain : app::PlayRefusal::name(r.why)}
            )
                .data();
        }
        case W::None:
        case W::Busy:
        case W::NotWired:
        case W::Slot:
        case W::PowerOn:
            if (r.setting.empty()) {
                return tasty_render(
                           out,
                           "replay refused: {}",
                           std::string_view{app::PlayRefusal::name(r.why)}
                )
                    .data();
            }
            return tasty_render(
                       out,
                       "replay refused: {} {}",
                       std::string_view{app::PlayRefusal::name(r.why)},
                       r.setting
            )
                .data();
        case W::NoCodec:
            return "replay refused: no movie format tasty knows plays this file on this core";
        case W::MovieIo:
            return "replay refused: the movie file cannot be read";
        case W::RomIo:
            return "replay refused: the ROM file cannot be read; check the --rom path";
        case W::RomSize:
            return "replay refused: the ROM is larger than tasty hashes, or not a ROM this "
                   "movie's format names";
        case W::Checksum:
            return "replay refused: the ROM on the card is not the one the movie was recorded "
                   "with";
        case W::Phase:
            return tasty_render(out, "replay refused: {}", std::string_view{kPhaseSentence}).data();
        case W::Lead:
            return "replay refused: the --lead value is outside what this movie's format allows";
        case W::Command:
            return "replay refused: this movie presses reset or power after it starts, which "
                   "tasty does not replay";
        case W::TooLong:
            return "replay refused: this movie is longer than tasty can play";
        case W::RomKind:
            return "replay refused: this disc image cannot be hashed; use a Redump .cue/.bin or "
                   "a chdman .chd of one";
        case W::RomChd:
            return tasty_render(out, "replay refused: {}", std::string_view{kChdSentence}).data();
        case W::kCount:
            return nullptr;
    }
    return nullptr;
}

const char* TastyRefusal::sentence() const noexcept {
    switch (why_) {
        case NoVerb:
            return "no verb (try tasty help)";
        case NoMovie:
            return "the verb needs a movie file";
        case MovieTooLong:
            return "the movie path is too long";
        case NoRecAction:
        case BadRecAction:
            return "rec needs start or stop";
        case UnknownVerb:
            return "unknown verb (try tasty help)";
        case BadRom:
            return "--rom needs a file";
        case BadCore:
            return "--core needs a file";
        case BadRecord:
            return "--record needs a directory";
        case BadLead:
            return "--lead needs a whole number of frames";
        case BadLinger:
            return "--linger needs a whole number of seconds";
        case BadVsyncAdjust:
            return "--vsync-adjust is 0 or 1";
        case BadStopAt:
            return "--stop-at needs a frame number above 0";
        case BadRamInit:
            return "--ram-init is zero, ff or random";
        case BadPhase:
            return "--phase needs a whole number of microseconds";
        case UnknownOption:
            return "unknown option (try tasty help)";
        case CheckNoRom:
            return "this verb needs --rom";
        case BadCodec:
            return "--codec is cscd or zmbv";
        case BadScale:
            return "--scale is auto, native or half";
        case BadEvery:
            return "--every is a whole number from 1 to 600";
        case BadFrom:
            return "--from needs a movie frame number, 0 or more";
        case BadTo:
            return "--to needs a movie frame number, 0 or more";
        case BadSegment:
            return "--segment is a size from 16M to 2G (bytes, or with K, M or G)";
        case BadRecBounds:
            return "--to must be after --from";
        case RecOptionsWithoutRecord:
            return "--codec, --scale, --motion, --every, --from, --to and --segment need --record";
        case BadMotion:
            return "--motion is auto, off, small or full";
        case SliceOnRecStart:
            return "--from and --to count movie frames; use them with tasty play --record";
        case BadSave:
            return "--save needs a file";
        case SaveNotPlay:
            return "--save seeds a movie's battery save; use it with tasty play";
        case LoopWithRecord:
            return "--loop and --record do not mix; drop --loop to record one pass";
        case RamInitUnsupported:
            return "this movie's core has no RAM-init setting; --ram-init works only where it has "
                   "one (NES: RAM Clear)";
        case PhaseOutOfRange:
            return kPhaseSentence;
        case RomNeedsChecksum:
            return "this movie names no ROM checksum; pass --rom";
        case RomPathTooLong:
            return "the matching ROM path is too long; pass --rom";
        case MenuLeftRunning:
            return "the menu was left running";
        case RecordNoTarget:
            return "that record path names no directory or file";
        case RecordIsDirectory:
            return "that .avi path is a directory; pass a file name or another directory";
        case RecordNoStem:
            return "that .avi name has no file stem; pass a name before .avi";
        case RecordParentFailed:
            return "cannot create the directory for that record";
        case RecordIsFile:
            return "that record path is a file; pass a directory or a name ending in .avi";
        case RecordDirFailed:
            return "cannot create that record directory";
        case RecordTooLong:
            return "that record path is too long";

        case NoCodec:
        case MovieUnreadable:
        case LeadOutOfRange:
        case RomNotFound:
        case RomSearchStopped:
        case RomUnplayable:
        case RomUnhashable:
            return nullptr;
        case kCount:
            return nullptr;
    }
    return nullptr;
}

void TastyRefusal::say() const noexcept {
    const char* const text = sentence();
    if (text == nullptr)
        return;

    tasty_sayf("tasty: {}", std::string_view{text});
}

const char* TastyRefusal::sentence_for(const Error& e) noexcept {
    if (const std::optional<TastyRefusal> r = of(e)) {
        if (const char* const text = r->sentence())
            return text;
    }
    return kBadArguments;
}

}
