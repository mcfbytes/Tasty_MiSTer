// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_cli.h"

#include <cstdio>
#include <optional>

#include "infra/parse_int.h"

namespace mister::fw {
namespace {

constexpr const char* kBadArguments = "bad arguments (try tasty help)";

[[nodiscard]] Ex<TastyArgs> fail(TastyRefusal r) noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), tasty_refusal_detail(r)});
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
    const std::optional<TastyRefusal> r = tasty_refusal_of(detail);
    if (!r)
        return kBadArguments;
    switch (*r) {
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
        case TastyRefusal::kCount:
            return kBadArguments;
    }
    return kBadArguments;
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

struct RecRow {
    std::string_view name;
    TastyVerb verb;
};

inline constexpr RecRow kRecRows[] = {
    {"start", TastyVerb::RecStart},
    {"stop", TastyVerb::RecStop},
};

using RamFill = cores::IReplayCore::RamFill;

struct FillRow {
    std::string_view name;
    RamFill fill;
};

inline constexpr FillRow kFillRows[] = {
    {"zero", RamFill::Zero},
    {"ff", RamFill::Ff},
    {"random", RamFill::Random},
};

[[nodiscard]] constexpr std::optional<RamFill> ram_fill_of(std::string_view word) noexcept {
    for (const FillRow& row : kFillRows)
        if (row.name == word)
            return row.fill;
    return std::nullopt;
}

enum class OptionArity : std::uint8_t {
    Flag,
    Accepted,
    Value,
    Finish,
};

using OptionTake = bool (*)(std::string_view word, TastyArgs& a) noexcept;

enum class OptionUse : std::uint8_t { Run, Record };

struct OptionRow {
    std::string_view name;
    OptionArity arity = OptionArity::Flag;
    TastyRefusal refusal = TastyRefusal::UnknownOption;
    OptionUse use = OptionUse::Run;
    std::optional<OptionTake> take{};
    std::optional<bool TastyArgs::*> field{};

    [[nodiscard]] static constexpr OptionRow flag(
        std::string_view name,
        bool TastyArgs::* field
    ) noexcept {
        OptionRow r{};
        r.name = name;
        r.arity = OptionArity::Flag;
        r.field = field;
        return r;
    }

    [[nodiscard]] static constexpr OptionRow accepted(std::string_view name) noexcept {
        OptionRow r{};
        r.name = name;
        r.arity = OptionArity::Accepted;
        return r;
    }

    [[nodiscard]] static constexpr OptionRow finish(std::string_view name) noexcept {
        OptionRow r{};
        r.name = name;
        r.arity = OptionArity::Finish;
        return r;
    }

    [[nodiscard]] static constexpr OptionRow valued(
        std::string_view name,
        TastyRefusal refusal,
        OptionTake take,
        OptionUse use = OptionUse::Run
    ) noexcept {
        OptionRow r{};
        r.name = name;
        r.arity = OptionArity::Value;
        r.refusal = refusal;
        r.use = use;
        r.take = take;
        return r;
    }

    [[nodiscard]] constexpr bool well_formed() const noexcept {
        switch (arity) {
            case OptionArity::Flag:
                return field.has_value() && !take.has_value();
            case OptionArity::Accepted:
            case OptionArity::Finish:
                return !field.has_value() && !take.has_value();
            case OptionArity::Value:
                return !field.has_value()
                    && take.has_value()
                    && refusal != TastyRefusal::UnknownOption
                    && refusal != TastyRefusal::kCount;
        }
        return false;
    }
};

[[nodiscard]] bool take_rom(std::string_view word, TastyArgs& a) noexcept {
    return a.rom.assign(word);
}

[[nodiscard]] bool take_core(std::string_view word, TastyArgs& a) noexcept {
    return a.core.assign(word);
}

[[nodiscard]] bool take_record(std::string_view word, TastyArgs& a) noexcept {
    app::PathText path{};
    if (!path.assign(word))
        return false;
    a.record = path;
    return true;
}

[[nodiscard]] bool take_save(std::string_view word, TastyArgs& a) noexcept {
    app::PathText save{};
    if (!save.assign(word))
        return false;
    a.save = save;
    return true;
}

[[nodiscard]] bool take_lead(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::int32_t> n = infra::parse_int<std::int32_t>(word);
    if (!n)
        return false;
    a.lead = app::FrameCount{*n};
    return true;
}

[[nodiscard]] bool take_phase(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::uint32_t> n = infra::parse_int<std::uint32_t>(word);
    if (!n)
        return false;
    a.phase_us = std::chrono::microseconds{*n};
    return true;
}

[[nodiscard]] bool take_stop_at(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::uint32_t> n = infra::parse_int<std::uint32_t>(word);
    if (!n || *n == 0)
        return false;
    a.stop_at = app::movie_frame_from_wire(*n);
    return true;
}

[[nodiscard]] bool take_linger(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::uint32_t> n = infra::parse_int<std::uint32_t>(word);
    if (!n)
        return false;
    a.linger = std::chrono::seconds{*n};
    return true;
}

[[nodiscard]] bool take_vsync_adjust(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::uint32_t> n = infra::parse_int<std::uint32_t>(word);
    if (!n || *n > kVsyncAdjustMatch)
        return false;
    a.vsync_adjust = *n == kVsyncAdjustOff ? app::VsyncAdjust::Off : app::VsyncAdjust::Match;
    return true;
}

[[nodiscard]] bool take_ram_init(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<RamFill> fill = ram_fill_of(word);
    if (!fill)
        return false;
    a.ram_fill = *fill;
    return true;
}

[[nodiscard]] bool take_codec(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<app::RecCodec> codec = app::parse_rec_codec(word);
    if (!codec)
        return false;
    a.rec.codec = *codec;
    return true;
}

[[nodiscard]] bool take_motion(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<app::RecMotion> motion = app::parse_rec_motion(word);
    if (!motion)
        return false;
    a.rec.motion = *motion;
    return true;
}

[[nodiscard]] bool take_scale(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<app::RecScale> scale = app::parse_rec_scale(word);
    if (!scale)
        return false;
    a.rec.scale = *scale;
    return true;
}

[[nodiscard]] bool take_every(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<app::RecEvery> every = app::parse_rec_every(word);
    if (!every)
        return false;
    a.rec.every = *every;
    return true;
}

[[nodiscard]] bool take_from(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::int32_t> n = infra::parse_int<std::int32_t>(word);
    if (!n || *n < 0)
        return false;
    a.rec.from_frame = app::MovieFrame{*n};
    return true;
}

[[nodiscard]] bool take_to(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<std::int32_t> n = infra::parse_int<std::int32_t>(word);
    if (!n || *n < 0)
        return false;
    a.rec.to_frame = app::MovieFrame{*n};
    return true;
}

[[nodiscard]] bool take_segment(std::string_view word, TastyArgs& a) noexcept {
    const std::optional<proto::FileSize> bytes = app::parse_rec_size(word);
    if (!bytes || bytes->empty() || !app::rec_segment_ok(*bytes))
        return false;
    a.rec.segment_bytes = *bytes;
    return true;
}

inline constexpr OptionRow kOptionRows[] = {
    OptionRow::valued("--rom", TastyRefusal::BadRom, take_rom),
    OptionRow::valued("--core", TastyRefusal::BadCore, take_core),
    OptionRow::valued("--record", TastyRefusal::BadRecord, take_record),
    OptionRow::valued("--save", TastyRefusal::BadSave, take_save),
    OptionRow::valued("--lead", TastyRefusal::BadLead, take_lead),
    OptionRow::valued("--phase", TastyRefusal::BadPhase, take_phase),
    OptionRow::valued("--stop-at", TastyRefusal::BadStopAt, take_stop_at),
    OptionRow::valued("--linger", TastyRefusal::BadLinger, take_linger),
    OptionRow::valued("--vsync-adjust", TastyRefusal::BadVsyncAdjust, take_vsync_adjust),
    OptionRow::valued("--ram-init", TastyRefusal::BadRamInit, take_ram_init),
    OptionRow::valued("--codec", TastyRefusal::BadCodec, take_codec, OptionUse::Record),
    OptionRow::valued("--motion", TastyRefusal::BadMotion, take_motion, OptionUse::Record),
    OptionRow::valued("--scale", TastyRefusal::BadScale, take_scale, OptionUse::Record),
    OptionRow::valued("--every", TastyRefusal::BadEvery, take_every, OptionUse::Record),
    OptionRow::valued("--from", TastyRefusal::BadFrom, take_from, OptionUse::Record),
    OptionRow::valued("--to", TastyRefusal::BadTo, take_to, OptionUse::Record),
    OptionRow::valued("--segment", TastyRefusal::BadSegment, take_segment, OptionUse::Record),
    OptionRow::flag("--stay", &TastyArgs::stay),
    OptionRow::flag("--loop", &TastyArgs::loop),
    OptionRow::flag("--strict", &TastyArgs::strict),
    OptionRow::flag("--no-splash", &TastyArgs::no_splash),
    OptionRow::accepted("--hashes"),
    OptionRow::flag("--hashes-only", &TastyArgs::hashes_only),
    OptionRow::finish("--help"),

    OptionRow::valued("--return-after", TastyRefusal::BadLinger, take_linger),
    OptionRow::finish("-h"),
};

template <class Row, std::size_t N>
[[nodiscard]] consteval bool words_named_once(const Row (&rows)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (rows[i].name.empty())
            return false;
        for (std::size_t j = i + 1; j < N; ++j)
            if (rows[i].name == rows[j].name)
                return false;
    }
    return true;
}

static_assert(words_named_once(kVerbRows), "every verb row names a distinct verb word");
static_assert(words_named_once(kRecRows), "every rec row names a distinct subverb");
static_assert(words_named_once(kFillRows), "every ram-init row names a distinct fill");
static_assert(words_named_once(kOptionRows), "every option row names a distinct option word");

[[nodiscard]] consteval bool options_are_well_formed() noexcept {
    for (const OptionRow& row : kOptionRows)
        if (!row.well_formed())
            return false;
    return true;
}

static_assert(options_are_well_formed(), "an option row must carry the payload its arity reads");

template <class Row, std::size_t N>
[[nodiscard]] const Row* row_named(const Row (&rows)[N], std::string_view word) noexcept {
    for (const Row& row : rows)
        if (row.name == word)
            return &row;
    return nullptr;
}

}

[[nodiscard]] Ex<TastyArgs> parse_tasty_args(std::span<const char* const> argv) noexcept {
    if (argv.size() <= kVerbArg)
        return fail(TastyRefusal::NoVerb);
    const std::string_view v0{argv[kVerbArg]};
    TastyArgs a{};
    std::size_t i = kOperandArg;
    if (v0 == "help" || v0 == "--help" || v0 == "-h") {
        a.verb = TastyVerb::Help;
        return a;
    }
    if (v0 == "rec") {
        if (argv.size() <= kOperandArg)
            return fail(TastyRefusal::NoRecAction);
        const RecRow* const rec = row_named(kRecRows, argv[kOperandArg]);
        if (rec == nullptr)
            return fail(TastyRefusal::BadRecAction);
        a.verb = rec->verb;
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

    bool record_option = false;
    while (i < argv.size()) {
        const OptionRow* const row = row_named(kOptionRows, argv[i]);
        if (row == nullptr)
            return fail(TastyRefusal::UnknownOption);
        switch (row->arity) {
            case OptionArity::Flag:
                a.*(*row->field) = true;
                ++i;
                break;
            case OptionArity::Accepted:
                ++i;
                break;
            case OptionArity::Finish:
                a.verb = TastyVerb::Help;
                return a;
            case OptionArity::Value: {
                if (i + 1 >= argv.size())
                    return fail(row->refusal);
                if (!(*row->take)(std::string_view{argv[i + 1]}, a))
                    return fail(row->refusal);
                if (row->use == OptionUse::Record)
                    record_option = true;
                i += kOptionWidth;
                break;
            }
        }
    }
    if (!app::rec_bounds_ok(a.rec))
        return fail(TastyRefusal::BadRecBounds);
    if (!a.record && record_option)
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
