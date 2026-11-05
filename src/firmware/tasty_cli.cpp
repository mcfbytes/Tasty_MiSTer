// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_cli.h"

#include <cstdio>

#include "infra/parse_int.h"

namespace mister::fw {
namespace {

bool eq(std::string_view a, const char* b) noexcept {
    return a == std::string_view(b);
}

[[nodiscard]] Ex<TastyArgs> fail(TastyRefusal::Code r) noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), r});
}

}

void tasty_print_usage(std::FILE* out) noexcept {
    std::fprintf(
        out,
        "usage: tasty play <movie> [--rom <file>] [options]\n"
        "       tasty info <movie>\n"
        "       tasty check <movie> --rom <file>\n"
        "       tasty status | stop\n"
        "       tasty rec start|stop [--record <path>]\n"
        "play options:\n"
        "  --rom <file>         ROM file (default: search /media/fat/games by checksum)\n"
        "  --core <rbf>         the core to load (default: the movie's system)\n"
        "  --lead <frames>      shift the whole movie by N frames\n"
        "  --phase <us>         pad write delay from vsync (default: half a frame)\n"
        "  --stop-at <frame>    play movie frames 0 to N-1, then end there\n"
        "  --loop               play the movie again each time it ends, until tasty stop;\n"
        "                       not with --record\n"
        "  --ram-init zero|ff|random\n"
        "                       NES RAM Clear for this run; other cores are refused\n"
        "  --save <file>        start the game's battery save as a copy of this file;\n"
        "                       without it the run's save starts empty. Your own saves\n"
        "                       are never opened or written\n"
        "  --linger <seconds>   wait after the last input before the menu (default 30)\n"
        "  --stay               never return to the menu on its own\n"
        "  --strict             refuse when a core setting differs from the movie's,\n"
        "                       or when direct_video is on, instead of setting it\n"
        "                       for this run\n"
        "  --no-splash          skip the logo\n"
        "  --vsync-adjust 0|1   scaler mode for the session\n"
        "  --record <dir>|<name>.avi\n"
        "                       record the run; a .avi name is the file stem\n"
        "  --codec cscd|zmbv    video codec (default cscd)\n"
        "  --motion auto|off|small|full\n"
        "                       zmbv motion search (default auto); zmbv only\n"
        "  --scale auto|native|half\n"
        "                       auto halves if the encode will not fit;\n"
        "                       native stays full; half starts halved\n"
        "  --every <n>          one AVI frame per n core frames (default 1);\n"
        "                       each kept frame lasts n core-frame times\n"
        "  --from <frame>       first movie frame kept in the AVI\n"
        "  --to <frame>         first movie frame left out of the AVI\n"
        "  --segment <size>     roll the AVI at this size, 16M..2G (default 1G)\n"
        "  --hashes             accepted; an AVI always writes the hash log beside it\n"
        "  --hashes-only        hash log only, no video\n"
        "The hash log lists every core frame. AVI frame N of segment S is the\n"
        "row whose segment and avi_frame columns are S and N; -1 was hashed\n"
        "and not written. The hash is the core picture, not the AVI picture.\n"
    );
}

const char* tasty_args_refusal(std::uint32_t detail) noexcept {
    switch (detail) {
        case TastyRefusal::NoVerb:
            return "no verb (try tasty help)";
        case TastyRefusal::NoMovie:
            return "the verb needs a movie file";
        case TastyRefusal::MovieTooLong:
            return "the movie path is too long";
        case TastyRefusal::NoRecAction:
        case TastyRefusal::BadRecAction:
            return "rec needs start or stop";
        case TastyRefusal::UnknownVerb:
            return "unknown verb (try tasty help)";
        case TastyRefusal::BadRom:
            return "--rom needs a file";
        case TastyRefusal::BadCore:
            return "--core needs a file";
        case TastyRefusal::BadRecord:
            return "--record needs a directory";
        case TastyRefusal::BadLead:
            return "--lead needs a whole number of frames";
        case TastyRefusal::BadLinger:
            return "--linger needs a whole number of seconds";
        case TastyRefusal::BadVsyncAdjust:
            return "--vsync-adjust is 0 or 1";
        case TastyRefusal::BadStopAt:
            return "--stop-at needs a frame number above 0";
        case TastyRefusal::BadRamInit:
            return "--ram-init is zero, ff or random";
        case TastyRefusal::BadPhase:
            return "--phase needs a whole number of microseconds";
        case TastyRefusal::UnknownOption:
            return "unknown option (try tasty help)";
        case TastyRefusal::CheckNoRom:
            return "this verb needs --rom";
        case TastyRefusal::BadCodec:
            return "--codec is cscd or zmbv";
        case TastyRefusal::BadScale:
            return "--scale is auto, native or half";
        case TastyRefusal::BadEvery:
            return "--every is a whole number from 1 to 600";
        case TastyRefusal::BadFrom:
            return "--from needs a movie frame number, 0 or more";
        case TastyRefusal::BadTo:
            return "--to needs a movie frame number, 0 or more";
        case TastyRefusal::BadSegment:
            return "--segment is a size from 16M to 2G (bytes, or with K, M or G)";
        case TastyRefusal::BadRecBounds:
            return "--to must be after --from";
        case TastyRefusal::RecOptionsWithoutRecord:
            return "--codec, --scale, --motion, --every, --from, --to and --segment need --record";
        case TastyRefusal::BadMotion:
            return "--motion is auto, off, small or full";
        case TastyRefusal::SliceOnRecStart:
            return "--from and --to count movie frames; use them with tasty play --record";
        case TastyRefusal::BadSave:
            return "--save needs a file";
        case TastyRefusal::SaveNotPlay:
            return "--save seeds a movie's battery save; use it with tasty play";
        default:
            return "bad arguments (try tasty help)";
    }
}

const char* tasty_verb_name(TastyVerb v) noexcept {
    switch (v) {
        case TastyVerb::Help:
            return "help";
        case TastyVerb::Play:
            return "play";
        case TastyVerb::Info:
            return "info";
        case TastyVerb::Check:
            return "check";
        case TastyVerb::Status:
            return "status";
        case TastyVerb::Stop:
            return "stop";
        case TastyVerb::RecStart:
            return "rec-start";
        case TastyVerb::RecStop:
            return "rec-stop";
    }
    return "?";
}

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept {

    constexpr std::size_t kVerbArg = 1;
    constexpr std::size_t kOperandArg = 2;
    constexpr std::size_t kFirstOptionArg = 3;

    constexpr std::size_t kOptionWidth = 2;
    TastyArgs a{};
    if (argv.size() <= kVerbArg)
        return fail(TastyRefusal::NoVerb);
    std::string_view v0{argv[kVerbArg]};
    std::size_t i = kOperandArg;
    if (eq(v0, "help") || eq(v0, "--help") || eq(v0, "-h")) {
        a.verb = TastyVerb::Help;
        return a;
    }
    if (eq(v0, "play")) {
        a.verb = TastyVerb::Play;
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoMovie);
        if (!a.movie.assign(argv[kOperandArg]))
            return fail(TastyRefusal::MovieTooLong);
        i = kFirstOptionArg;
    } else if (eq(v0, "info")) {
        a.verb = TastyVerb::Info;
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoMovie);
        if (!a.movie.assign(argv[kOperandArg]))
            return fail(TastyRefusal::MovieTooLong);
        i = kFirstOptionArg;
    } else if (eq(v0, "check")) {
        a.verb = TastyVerb::Check;
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoMovie);
        if (!a.movie.assign(argv[kOperandArg]))
            return fail(TastyRefusal::MovieTooLong);
        i = kFirstOptionArg;
    } else if (eq(v0, "status")) {
        a.verb = TastyVerb::Status;
    } else if (eq(v0, "stop")) {
        a.verb = TastyVerb::Stop;
    } else if (eq(v0, "rec")) {
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoRecAction);
        std::string_view sub{argv[kOperandArg]};
        if (eq(sub, "start")) {
            a.verb = TastyVerb::RecStart;
            i = kFirstOptionArg;
        } else if (eq(sub, "stop")) {
            a.verb = TastyVerb::RecStop;
            i = kFirstOptionArg;
        } else {
            return fail(TastyRefusal::BadRecAction);
        }
    } else {
        return fail(TastyRefusal::UnknownVerb);
    }

    bool rec_opt = false;
    while (i < argv.size()) {
        std::string_view o{argv[i]};
        auto need = [&]() -> const char* {
            if (i + 1 >= argv.size())
                return nullptr;
            return argv[i + 1];
        };
        if (eq(o, "--rom")) {
            const char* p = need();
            if (p == nullptr || !a.rom.assign(p))
                return fail(TastyRefusal::BadRom);
            i += kOptionWidth;
        } else if (eq(o, "--core")) {
            const char* p = need();
            if (p == nullptr || !a.core.assign(p))
                return fail(TastyRefusal::BadCore);
            i += kOptionWidth;
        } else if (eq(o, "--record")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadRecord);
            app::PathText path{};
            if (!path.assign(p))
                return fail(TastyRefusal::BadRecord);
            a.record = path;
            i += kOptionWidth;
        } else if (eq(o, "--lead")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadLead);
            const auto n = infra::parse_int<std::int32_t>(p);
            if (!n)
                return fail(TastyRefusal::BadLead);
            a.lead = *n;
            i += kOptionWidth;
        } else if (eq(o, "--phase")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadPhase);
            const auto n = infra::parse_int<std::uint32_t>(p);
            if (!n)
                return fail(TastyRefusal::BadPhase);
            a.phase_us = *n;
            i += kOptionWidth;
        } else if (eq(o, "--stop-at")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadStopAt);
            const auto n = infra::parse_int<std::uint32_t>(p);
            if (!n || *n == 0)
                return fail(TastyRefusal::BadStopAt);
            a.stop_at = *n;
            i += kOptionWidth;
        } else if (eq(o, "--linger") || eq(o, "--return-after")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadLinger);
            const auto n = infra::parse_int<std::uint32_t>(p);
            if (!n)
                return fail(TastyRefusal::BadLinger);
            a.return_after_s = *n;
            i += kOptionWidth;
        } else if (eq(o, "--vsync-adjust")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadVsyncAdjust);
            const auto n = infra::parse_int<std::uint32_t>(p);
            if (!n || *n > 1)
                return fail(TastyRefusal::BadVsyncAdjust);
            a.vsync_adjust = *n == 0 ? app::VsyncAdjust::Off : app::VsyncAdjust::Match;
            i += kOptionWidth;
        } else if (eq(o, "--stay")) {
            a.stay = true;
            ++i;
        } else if (eq(o, "--loop")) {
            a.loop = true;
            ++i;
        } else if (eq(o, "--ram-init")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadRamInit);
            const std::string_view s{p};
            using Fill = cores::IMovieCodec::RamFill;
            if (s == "zero")
                a.ram_fill = Fill::Zero;
            else if (s == "ff")
                a.ram_fill = Fill::Ff;
            else if (s == "random")
                a.ram_fill = Fill::Random;
            else
                return fail(TastyRefusal::BadRamInit);
            i += kOptionWidth;
        } else if (eq(o, "--save")) {
            const char* p = need();
            app::PathText save{};
            if (p == nullptr || !save.assign(p))
                return fail(TastyRefusal::BadSave);
            a.save = save;
            i += kOptionWidth;
        } else if (eq(o, "--strict")) {
            a.strict = true;
            ++i;
        } else if (eq(o, "--help") || eq(o, "-h")) {
            a.verb = TastyVerb::Help;
            return a;
        } else if (eq(o, "--no-splash")) {
            a.no_splash = true;
            ++i;
        } else if (eq(o, "--hashes")) {
            ++i;
        } else if (eq(o, "--hashes-only")) {
            a.hashes_only = true;
            ++i;
        } else if (eq(o, "--codec")) {
            const char* p = need();
            const auto c = p != nullptr ? app::parse_rec_codec(p) : std::nullopt;
            if (!c)
                return fail(TastyRefusal::BadCodec);
            a.rec.codec = *c;
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--motion")) {
            const char* p = need();
            const auto m = p != nullptr ? app::parse_rec_motion(p) : std::nullopt;
            if (!m)
                return fail(TastyRefusal::BadMotion);
            a.rec.motion = *m;
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--scale")) {
            const char* p = need();
            const auto s = p != nullptr ? app::parse_rec_scale(p) : std::nullopt;
            if (!s)
                return fail(TastyRefusal::BadScale);
            a.rec.scale = *s;
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--every")) {
            const char* p = need();
            const auto n = p != nullptr ? app::parse_rec_every(p) : std::nullopt;
            if (!n)
                return fail(TastyRefusal::BadEvery);
            a.rec.every = *n;
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--from")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadFrom);
            const auto n = infra::parse_int<std::int32_t>(p);
            if (!n || *n < 0)
                return fail(TastyRefusal::BadFrom);
            a.rec.from_frame = app::MovieFrame{*n};
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--to")) {
            const char* p = need();
            if (p == nullptr)
                return fail(TastyRefusal::BadTo);
            const auto n = infra::parse_int<std::int32_t>(p);
            if (!n || *n < 0)
                return fail(TastyRefusal::BadTo);
            a.rec.to_frame = app::MovieFrame{*n};
            rec_opt = true;
            i += kOptionWidth;
        } else if (eq(o, "--segment")) {
            const char* p = need();
            const auto n = p != nullptr ? app::parse_rec_size(p) : std::nullopt;
            if (!n || *n == 0 || !app::rec_segment_ok(*n))
                return fail(TastyRefusal::BadSegment);
            a.rec.segment_bytes = *n;
            rec_opt = true;
            i += kOptionWidth;
        } else {
            return fail(TastyRefusal::UnknownOption);
        }
    }
    if (!app::rec_bounds_ok(a.rec))
        return fail(TastyRefusal::BadRecBounds);
    if (!a.record && rec_opt)
        return fail(TastyRefusal::RecOptionsWithoutRecord);

    if (a.verb == TastyVerb::RecStart
        && (a.rec.from_frame >= app::MovieFrame{} || a.rec.to_frame >= app::MovieFrame{}))
        return fail(TastyRefusal::SliceOnRecStart);
    if (a.verb == TastyVerb::Check && a.rom.empty())
        return fail(TastyRefusal::CheckNoRom);
    if (a.save && a.verb != TastyVerb::Play)
        return fail(TastyRefusal::SaveNotPlay);
    return a;
}

}
