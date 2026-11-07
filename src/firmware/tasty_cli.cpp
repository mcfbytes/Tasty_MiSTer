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

namespace {

constexpr std::size_t kVerbArg = 1;
constexpr std::size_t kOperandArg = 2;
constexpr std::size_t kFirstOptionArg = 3;

constexpr std::size_t kOptionWidth = 2;

constexpr std::uint32_t kVsyncAdjustOff = 0;
constexpr std::uint32_t kVsyncAdjustMatch = 1;

struct VerbRow {
    std::string_view name;
    TastyVerb verb;
    bool takes_movie;
};

inline constexpr VerbRow kVerbRows[] = {
    {"play", TastyVerb::Play, true},
    {"info", TastyVerb::Info, true},
    {"check", TastyVerb::Check, true},
    {"status", TastyVerb::Status, false},
    {"stop", TastyVerb::Stop, false},
};

enum class OptStep : std::uint8_t { NotMine, Flag, Valued, Refused, Finish };

struct OptResult {
    OptStep step = OptStep::NotMine;
    TastyRefusal::Code bad = TastyRefusal::UnknownOption;
};

constexpr OptResult kNotMine{};

constexpr OptResult taken_flag() noexcept {
    return OptResult{OptStep::Flag, TastyRefusal::UnknownOption};
}

constexpr OptResult taken_valued() noexcept {
    return OptResult{OptStep::Valued, TastyRefusal::UnknownOption};
}

constexpr OptResult refused(TastyRefusal::Code bad) noexcept {
    return OptResult{OptStep::Refused, bad};
}

[[nodiscard]] OptResult take_path_option(std::string_view o, const char* p, TastyArgs& a) noexcept {
    if (eq(o, "--rom")) {
        if (p == nullptr || !a.rom.assign(p))
            return refused(TastyRefusal::BadRom);
        return taken_valued();
    }
    if (eq(o, "--core")) {
        if (p == nullptr || !a.core.assign(p))
            return refused(TastyRefusal::BadCore);
        return taken_valued();
    }
    if (eq(o, "--record")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadRecord);
        app::PathText path{};
        if (!path.assign(p))
            return refused(TastyRefusal::BadRecord);
        a.record = path;
        return taken_valued();
    }
    if (eq(o, "--save")) {
        app::PathText save{};
        if (p == nullptr || !save.assign(p))
            return refused(TastyRefusal::BadSave);
        a.save = save;
        return taken_valued();
    }
    return kNotMine;
}

[[nodiscard]] OptResult take_time_option(std::string_view o, const char* p, TastyArgs& a) noexcept {
    if (eq(o, "--lead")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadLead);
        const auto n = infra::parse_int<std::int32_t>(p);
        if (!n)
            return refused(TastyRefusal::BadLead);
        a.lead = app::MovieFrame{*n};
        return taken_valued();
    }
    if (eq(o, "--phase")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadPhase);
        const auto n = infra::parse_int<std::uint32_t>(p);
        if (!n)
            return refused(TastyRefusal::BadPhase);
        a.phase_us = std::chrono::microseconds{*n};
        return taken_valued();
    }
    if (eq(o, "--stop-at")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadStopAt);
        const auto n = infra::parse_int<std::uint32_t>(p);
        if (!n || *n == 0)
            return refused(TastyRefusal::BadStopAt);
        a.stop_at = app::movie_frame_from_wire(*n);
        return taken_valued();
    }
    if (eq(o, "--linger") || eq(o, "--return-after")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadLinger);
        const auto n = infra::parse_int<std::uint32_t>(p);
        if (!n)
            return refused(TastyRefusal::BadLinger);
        a.return_after_s = std::chrono::seconds{*n};
        return taken_valued();
    }
    if (eq(o, "--vsync-adjust")) {
        if (p == nullptr)
            return refused(TastyRefusal::BadVsyncAdjust);
        const auto n = infra::parse_int<std::uint32_t>(p);
        if (!n || *n > kVsyncAdjustMatch)
            return refused(TastyRefusal::BadVsyncAdjust);
        a.vsync_adjust = *n == kVsyncAdjustOff ? app::VsyncAdjust::Off : app::VsyncAdjust::Match;
        return taken_valued();
    }
    return kNotMine;
}

[[nodiscard]] OptResult take_fill_option(std::string_view o, const char* p, TastyArgs& a) noexcept {
    if (!eq(o, "--ram-init"))
        return kNotMine;
    if (p == nullptr)
        return refused(TastyRefusal::BadRamInit);
    const std::string_view s{p};
    using Fill = cores::IMovieCodec::RamFill;
    if (s == "zero")
        a.ram_fill = Fill::Zero;
    else if (s == "ff")
        a.ram_fill = Fill::Ff;
    else if (s == "random")
        a.ram_fill = Fill::Random;
    else
        return refused(TastyRefusal::BadRamInit);
    return taken_valued();
}

[[nodiscard]] OptResult take_record_option(
    std::string_view o,
    const char* p,
    TastyArgs& a,
    bool& rec_opt
) noexcept {
    using R = TastyRefusal;
    if (eq(o, "--codec")) {
        const auto c = p != nullptr ? app::parse_rec_codec(p) : std::nullopt;
        if (!c)
            return refused(R::BadCodec);
        a.rec.codec = *c;
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--motion")) {
        const auto m = p != nullptr ? app::parse_rec_motion(p) : std::nullopt;
        if (!m)
            return refused(R::BadMotion);
        a.rec.motion = *m;
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--scale")) {
        const auto s = p != nullptr ? app::parse_rec_scale(p) : std::nullopt;
        if (!s)
            return refused(R::BadScale);
        a.rec.scale = *s;
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--every")) {
        const auto n = p != nullptr ? app::parse_rec_every(p) : std::nullopt;
        if (!n)
            return refused(R::BadEvery);
        a.rec.every = *n;
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--from")) {
        if (p == nullptr)
            return refused(R::BadFrom);
        const auto n = infra::parse_int<std::int32_t>(p);
        if (!n || *n < 0)
            return refused(R::BadFrom);
        a.rec.from_frame = app::MovieFrame{*n};
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--to")) {
        if (p == nullptr)
            return refused(R::BadTo);
        const auto n = infra::parse_int<std::int32_t>(p);
        if (!n || *n < 0)
            return refused(R::BadTo);
        a.rec.to_frame = app::MovieFrame{*n};
        rec_opt = true;
        return taken_valued();
    }
    if (eq(o, "--segment")) {
        const auto n = p != nullptr ? app::parse_rec_size(p) : std::nullopt;
        if (!n || *n == 0 || !app::rec_segment_ok(*n))
            return refused(R::BadSegment);
        a.rec.segment_bytes = *n;
        rec_opt = true;
        return taken_valued();
    }
    return kNotMine;
}

[[nodiscard]] OptResult take_flag_option(std::string_view o, TastyArgs& a) noexcept {
    if (eq(o, "--stay")) {
        a.stay = true;
        return taken_flag();
    }
    if (eq(o, "--loop")) {
        a.loop = true;
        return taken_flag();
    }
    if (eq(o, "--strict")) {
        a.strict = true;
        return taken_flag();
    }
    if (eq(o, "--no-splash")) {
        a.no_splash = true;
        return taken_flag();
    }
    if (eq(o, "--hashes"))
        return taken_flag();
    if (eq(o, "--hashes-only")) {
        a.hashes_only = true;
        return taken_flag();
    }
    if (eq(o, "--help") || eq(o, "-h")) {
        a.verb = TastyVerb::Help;
        return OptResult{OptStep::Finish, TastyRefusal::UnknownOption};
    }
    return kNotMine;
}

}

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept {
    if (argv.size() <= kVerbArg)
        return fail(TastyRefusal::NoVerb);
    std::string_view v0{argv[kVerbArg]};
    TastyArgs a{};
    std::size_t i = kOperandArg;
    if (eq(v0, "help") || eq(v0, "--help") || eq(v0, "-h")) {
        a.verb = TastyVerb::Help;
        return a;
    }
    if (eq(v0, "rec")) {
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoRecAction);
        const std::string_view sub{argv[kOperandArg]};
        if (eq(sub, "start"))
            a.verb = TastyVerb::RecStart;
        else if (eq(sub, "stop"))
            a.verb = TastyVerb::RecStop;
        else
            return fail(TastyRefusal::BadRecAction);
        i = kFirstOptionArg;
    } else {
        const VerbRow* row = nullptr;
        for (const VerbRow& r : kVerbRows)
            if (v0 == r.name) {
                row = &r;
                break;
            }
        if (row == nullptr)
            return fail(TastyRefusal::UnknownVerb);
        a.verb = row->verb;
        if (row->takes_movie) {
            if (argv.size() <= kOperandArg)
                return fail(TastyRefusal::NoMovie);
            if (!a.movie.assign(argv[kOperandArg]))
                return fail(TastyRefusal::MovieTooLong);
            i = kFirstOptionArg;
        }
    }

    bool rec_opt = false;
    while (i < argv.size()) {
        const std::string_view o{argv[i]};
        const char* const p = i + 1 < argv.size() ? argv[i + 1] : nullptr;
        OptResult r = take_path_option(o, p, a);
        if (r.step == OptStep::NotMine)
            r = take_time_option(o, p, a);
        if (r.step == OptStep::NotMine)
            r = take_fill_option(o, p, a);
        if (r.step == OptStep::NotMine)
            r = take_record_option(o, p, a, rec_opt);
        if (r.step == OptStep::NotMine)
            r = take_flag_option(o, a);
        switch (r.step) {
            case OptStep::NotMine:
                return fail(TastyRefusal::UnknownOption);
            case OptStep::Refused:
                return fail(r.bad);
            case OptStep::Finish:
                return a;
            case OptStep::Flag:
                ++i;
                break;
            case OptStep::Valued:
                i += kOptionWidth;
                break;
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
