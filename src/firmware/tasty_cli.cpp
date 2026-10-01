// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_cli.h"

#include <charconv>
#include <cstdio>

namespace mister::fw {
namespace {

bool eq(std::string_view a, const char* b) noexcept { return a == std::string_view(b); }

std::optional<std::uint32_t> u32(std::string_view s) noexcept {
    std::uint32_t v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

std::optional<std::int32_t> i32(std::string_view s) noexcept {
    std::int32_t v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

[[nodiscard]] Ex<TastyArgs> fail(std::uint32_t d) noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
}

}  // namespace

void tasty_print_usage(std::FILE* out) noexcept {
    std::fprintf(
        out, "usage: tasty play <movie> [--rom <file>] [options]\n"
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
             "and not written. The hash is the core picture, not the AVI picture.\n");
}

const char* tasty_args_refusal(std::uint32_t detail) noexcept {
    switch (detail) {
        case 1:
            return "no verb (try tasty help)";
        case 2:
            return "the verb needs a movie file";
        case 3:
            return "the movie path is too long";
        case 4:
        case 5:
            return "rec needs start or stop";
        case 6:
            return "unknown verb (try tasty help)";
        case 10:
            return "--rom needs a file";
        case 11:
            return "--core needs a file";
        case 12:
            return "--record needs a directory";
        case 13:
            return "--lead needs a whole number of frames";
        case 14:
            return "--linger needs a whole number of seconds";
        case 15:
            return "--vsync-adjust is 0 or 1";
        case 16:
            return "--stop-at needs a frame number above 0";
        case 17:
            return "--ram-init is zero, ff or random";
        case 18:
            return "--phase needs a whole number of microseconds";
        case 20:
            return "unknown option (try tasty help)";
        case 21:
            return "this verb needs --rom";
        case 22:
            return "--codec is cscd or zmbv";
        case 23:
            return "--scale is auto, native or half";
        case 24:
            return "--every is a whole number from 1 to 600";
        case 25:
            return "--from needs a movie frame number, 0 or more";
        case 26:
            return "--to needs a movie frame number, 0 or more";
        case 27:
            return "--segment is a size from 16M to 2G (bytes, or with K, M or G)";
        case 28:
            return "--to must be after --from";
        case 29:
            return "--codec, --scale, --motion, --every, --from, --to and --segment need --record";
        case 30:
            return "--motion is auto, off, small or full";
        case 31:
            return "--from and --to count movie frames; use them with tasty play --record";
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
    TastyArgs a{};
    if (argv.size() < 2) return fail(1);
    std::string_view v0{argv[1]};
    std::size_t i = 2;
    if (eq(v0, "help") || eq(v0, "--help") || eq(v0, "-h")) {
        a.verb = TastyVerb::Help;
        return a;
    }
    if (eq(v0, "play")) {
        a.verb = TastyVerb::Play;
        if (argv.size() < 3) return fail(2);
        if (!a.movie.assign(argv[2])) return fail(3);
        i = 3;
    } else if (eq(v0, "info")) {
        a.verb = TastyVerb::Info;
        if (argv.size() < 3) return fail(2);
        if (!a.movie.assign(argv[2])) return fail(3);
        i = 3;
    } else if (eq(v0, "check")) {
        a.verb = TastyVerb::Check;
        if (argv.size() < 3) return fail(2);
        if (!a.movie.assign(argv[2])) return fail(3);
        i = 3;
    } else if (eq(v0, "status")) {
        a.verb = TastyVerb::Status;
    } else if (eq(v0, "stop")) {
        a.verb = TastyVerb::Stop;
    } else if (eq(v0, "rec")) {
        if (argv.size() < 3) return fail(4);
        std::string_view sub{argv[2]};
        if (eq(sub, "start")) {
            a.verb = TastyVerb::RecStart;
            i = 3;
        } else if (eq(sub, "stop")) {
            a.verb = TastyVerb::RecStop;
            i = 3;
        } else {
            return fail(5);
        }
    } else {
        return fail(6);
    }

    bool rec_opt = false;
    while (i < argv.size()) {
        std::string_view o{argv[i]};
        auto need = [&]() -> const char* {
            if (i + 1 >= argv.size()) return nullptr;
            return argv[i + 1];
        };
        if (eq(o, "--rom")) {
            const char* p = need();
            if (p == nullptr || !a.rom.assign(p)) return fail(10);
            i += 2;
        } else if (eq(o, "--core")) {
            const char* p = need();
            if (p == nullptr || !a.core.assign(p)) return fail(11);
            i += 2;
        } else if (eq(o, "--record")) {
            const char* p = need();
            if (p == nullptr) return fail(12);
            app::PathText path{};
            if (!path.assign(p)) return fail(12);
            a.record = path;
            i += 2;
        } else if (eq(o, "--lead")) {
            const char* p = need();
            if (p == nullptr) return fail(13);
            const auto n = i32(p);
            if (!n) return fail(13);
            a.lead = *n;
            i += 2;
        } else if (eq(o, "--phase")) {
            const char* p = need();
            if (p == nullptr) return fail(18);
            const auto n = u32(p);
            if (!n) return fail(18);
            a.phase_us = *n;
            i += 2;
        } else if (eq(o, "--stop-at")) {
            const char* p = need();
            if (p == nullptr) return fail(16);
            const auto n = u32(p);
            if (!n || *n == 0) return fail(16);
            a.stop_at = *n;
            i += 2;
        } else if (eq(o, "--linger") || eq(o, "--return-after")) {
            const char* p = need();
            if (p == nullptr) return fail(14);
            const auto n = u32(p);
            if (!n) return fail(14);
            a.return_after_s = *n;
            i += 2;
        } else if (eq(o, "--vsync-adjust")) {
            const char* p = need();
            if (p == nullptr) return fail(15);
            const auto n = u32(p);
            if (!n || *n > 1) return fail(15);
            a.vsync_adjust = static_cast<std::uint8_t>(*n);
            i += 2;
        } else if (eq(o, "--stay")) {
            a.stay = true;
            ++i;
        } else if (eq(o, "--loop")) {
            a.loop = true;
            ++i;
        } else if (eq(o, "--ram-init")) {
            const char* p = need();
            if (p == nullptr) return fail(17);
            const std::string_view s{p};
            using Fill = cores::IMovieCodec::RamFill;
            if (s == "zero")
                a.ram_fill = Fill::Zero;
            else if (s == "ff")
                a.ram_fill = Fill::Ff;
            else if (s == "random")
                a.ram_fill = Fill::Random;
            else
                return fail(17);
            i += 2;
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
            if (!c) return fail(22);
            a.rec.codec = *c;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--motion")) {
            const char* p = need();
            const auto m = p != nullptr ? app::parse_rec_motion(p) : std::nullopt;
            if (!m) return fail(30);
            a.rec.motion = *m;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--scale")) {
            const char* p = need();
            const auto s = p != nullptr ? app::parse_rec_scale(p) : std::nullopt;
            if (!s) return fail(23);
            a.rec.scale = *s;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--every")) {
            const char* p = need();
            const auto n = p != nullptr ? app::parse_rec_every(p) : std::nullopt;
            if (!n) return fail(24);
            a.rec.every = *n;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--from")) {
            const char* p = need();
            if (p == nullptr) return fail(25);
            const auto n = i32(p);
            if (!n || *n < 0) return fail(25);
            a.rec.from_frame = *n;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--to")) {
            const char* p = need();
            if (p == nullptr) return fail(26);
            const auto n = i32(p);
            if (!n || *n < 0) return fail(26);
            a.rec.to_frame = *n;
            rec_opt = true;
            i += 2;
        } else if (eq(o, "--segment")) {
            const char* p = need();
            const auto n = p != nullptr ? app::parse_rec_size(p) : std::nullopt;
            if (!n || *n == 0 || !app::rec_segment_ok(*n)) return fail(27);
            a.rec.segment_bytes = *n;
            rec_opt = true;
            i += 2;
        } else {
            return fail(20);
        }
    }
    if (!app::rec_bounds_ok(a.rec)) return fail(28);
    if (!a.record && rec_opt) return fail(29);

    if (a.verb == TastyVerb::RecStart && (a.rec.from_frame >= 0 || a.rec.to_frame >= 0))
        return fail(31);
    if (a.verb == TastyVerb::Check && a.rom.empty()) return fail(21);
    return a;
}

}  // namespace mister::fw
