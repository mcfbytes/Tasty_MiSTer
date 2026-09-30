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
    std::fprintf(out,
                 "usage: tasty play <movie> --rom <file> [options]\n"
                 "       tasty info <movie>\n"
                 "       tasty check <movie> --rom <file>\n"
                 "       tasty status | stop\n"
                 "       tasty rec start|stop [--record <dir>]\n"
                 "play options:\n"
                 "  --core <rbf>         the core to load (default: the movie's system)\n"
                 "  --lead <frames>      shift the whole movie by N frames\n"
                 "  --stop-at <frame>    play movie frames 0 to N-1, then end there\n"
                 "  --linger <seconds>   wait after the last input before the menu (default 30)\n"
                 "  --stay               never return to the menu on its own\n"
                 "  --strict             refuse when a core setting differs from the movie's,\n"
                 "                       instead of setting it for this run\n"
                 "  --no-splash          skip the logo\n"
                 "  --vsync-adjust 0|1   scaler mode for the session\n"
                 "  --record <dir>       record the run (AVI)\n"
                 "  --hashes             also write a per-frame hash log\n"
                 "  --hashes-only        hash log only, no video\n");
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
            if (p == nullptr || !a.record.assign(p)) return fail(12);
            i += 2;
        } else if (eq(o, "--lead")) {
            const char* p = need();
            if (p == nullptr) return fail(13);
            const auto n = i32(p);
            if (!n) return fail(13);
            a.lead = *n;
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
            a.hashes = true;
            ++i;
        } else if (eq(o, "--hashes-only")) {
            a.hashes_only = true;
            ++i;
        } else {
            return fail(20);
        }
    }
    if (a.verb == TastyVerb::Check && a.rom.empty()) return fail(21);
    if (a.verb == TastyVerb::Play && a.rom.empty()) return fail(21);
    return a;
}

}  // namespace mister::fw
