// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"
#include "svc/cvt.h"
#include "svc/pll_params.h"
#include "svc/pll_solver.h"
#include "svc/yc_table.h"

#include <charconv>

#include "infra/ascii.h"
#include "video_service_detail.h"

namespace mister::svc {

namespace {

std::optional<std::uint32_t> parse_u32_base0(std::string_view s) {
    constexpr int kBaseDecimal = 10;
    constexpr int kBaseOctal = 8;
    constexpr int kBaseHex = 16;
    constexpr std::size_t kPrefix0xLen = 2;
    constexpr std::size_t kPrefix0Len = 1;
    if (s.empty())
        return std::nullopt;
    int base = kBaseDecimal;
    if (s.size() > kPrefix0xLen && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = kBaseHex;
        s.remove_prefix(kPrefix0xLen);
    } else if (s.size() > kPrefix0Len && s[0] == '0') {
        base = kBaseOctal;
        s.remove_prefix(kPrefix0Len);
    }
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    std::uint32_t out = 0;
    auto [ptr, ec] = std::from_chars(first, last, out, base);
    if (ec != std::errc{} || ptr != last)
        return std::nullopt;
    return out;
}

std::optional<double> parse_double_all(std::string_view s) {
    if (s.empty())
        return std::nullopt;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    double out = 0.0;
    auto [ptr, ec] = std::from_chars(first, last, out);
    if (ec != std::errc{} || ptr != last)
        return std::nullopt;
    return out;
}

std::int64_t strtoull_prefix(std::string_view s) {
    constexpr unsigned kBaseDecimal = 10;
    constexpr unsigned kBaseOctal = 8;
    constexpr unsigned kBaseHex = 16;
    constexpr std::size_t kPrefix0xLen = 2;
    constexpr unsigned kLetterDigitBase = 10;
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    unsigned base = kBaseDecimal;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = kBaseHex;
        i += kPrefix0xLen;
    } else if (i < s.size() && s[i] == '0') {
        base = kBaseOctal;
        ++i;
    }
    std::uint64_t v = 0;
    for (; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<unsigned>(c - 'a') + kLetterDigitBase;
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<unsigned>(c - 'A') + kLetterDigitBase;
        else
            break;
        if (digit >= base)
            break;
        v = v * base + digit;
    }
    if (neg)
        v = ~v + 1u;
    return static_cast<std::int64_t>(v);
}

constexpr std::size_t kMaxTokens = 32;

struct TokenList {
    std::array<std::string_view, kMaxTokens> items{};
    std::size_t count = 0;
};

TokenList tokenize_commas(std::string_view s) {
    TokenList out{};
    std::size_t i = 0;
    while (i < s.size() && out.count < kMaxTokens) {
        while (i < s.size() && s[i] == ',')
            ++i;
        if (i >= s.size())
            break;
        const std::size_t start = i;
        while (i < s.size() && s[i] != ',')
            ++i;
        out.items[out.count++] = s.substr(start, i - start);
    }
    return out;
}

constexpr std::size_t kArityPreset = 1;
constexpr std::size_t kArityCvtFractional = 2;
constexpr std::size_t kArityCvt = 3;
constexpr std::size_t kArityTimings = 9;
constexpr std::size_t kArityTimingsPolarity = 11;
constexpr std::size_t kArityRaw = 21;

constexpr std::size_t kTokenWidth = 0;
constexpr std::size_t kTokenHeight = 1;
constexpr std::size_t kTokenRate = 2;
constexpr std::size_t kTokenPixelClockKhz = 8;
constexpr std::size_t kTokenShortHpol = 9;
constexpr std::size_t kTokenShortVpol = 10;
constexpr double kKhzPerMhz = 1000.0;

constexpr std::uint32_t kTokenByteMask = 0xFF;

struct ModeSpec {
    std::array<std::uint32_t, kMaxTokens> val{};
    TokenList integers{};
    TokenList flags{};
    double rate = 0.0;
};

std::optional<ModeSpec> scan_mode(std::string_view spec) {
    if (spec.empty())
        return std::nullopt;
    const TokenList tokens = tokenize_commas(spec);
    if (tokens.count == 0)
        return std::nullopt;
    ModeSpec s{};
    std::size_t run = 0;
    for (; run < tokens.count; ++run) {
        const auto parsed = parse_u32_base0(tokens.items[run]);
        if (!parsed)
            break;
        s.val[run] = *parsed;
    }

    if (run == kArityCvtFractional && tokens.count > kArityCvtFractional) {
        if (const auto parsed = parse_double_all(tokens.items[run])) {
            s.rate = *parsed;
            ++run;
        }
    }
    for (std::size_t i = 0; i < tokens.count; ++i) {
        TokenList& side = (i < run) ? s.integers : s.flags;
        side.items[side.count++] = tokens.items[i];
    }
    return s;
}

[[nodiscard]] Ex<void> apply_mode_flags(const ModeSpec& s, Modeline& m) {
    for (std::size_t i = 0; i < s.flags.count; ++i) {
        const std::string_view flag = s.flags.items[i];
        if (infra::ieq(flag, "+vsync"))
            m.vpol = true;
        else if (infra::ieq(flag, "-vsync"))
            m.vpol = false;
        else if (infra::ieq(flag, "+hsync"))
            m.hpol = true;
        else if (infra::ieq(flag, "-hsync"))
            m.hpol = false;
        else if (infra::ieq(flag, "cvt"))
            m.rb = rb_byte(ReducedBlanking::Off);
        else if (infra::ieq(flag, "cvtrb"))
            m.rb = rb_byte(ReducedBlanking::On);
        else if (infra::ieq(flag, "pr"))
            m.pixel_repeat = true;
        else
            return std::unexpected(
                Error{
                    Errc::bad_format,
                    ERR_SITE(),
                    static_cast<std::uint32_t>(s.integers.count + i)
                }
            );
    }
    return {};
}

}

std::vector<YcMode> parse_yc(std::string_view text, std::string_view core_name) {
    std::vector<YcMode> out;
    out.reserve(kYcMaxEntries);

    std::size_t pos = 0;
    while (out.size() < kYcMaxEntries && pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? text.substr(pos)
                                                               : text.substr(pos, nl - pos);

        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        const bool match = infra::istarts_with(line, core_name);
        if (match) {
            const std::size_t eq = line.find('=');

            if (eq != std::string_view::npos) {
                std::size_t v = eq + 1;
                while (v < line.size() && (line[v] == '=' || line[v] == ' ' || line[v] == '\t')) {
                    ++v;
                }
                YcMode e{};
                (void)e.key.assign(line.substr(0, eq));
                e.phase_inc = strtoull_prefix(line.substr(v));

                if (e.phase_inc != 0)
                    out.push_back(e);
            }
        }

        if (nl == std::string_view::npos)
            break;
        pos = nl + 1;
    }
    return out;
}

std::optional<std::int64_t> yc_lookup(
    std::span<const YcMode> table,
    std::string_view key,
    std::string_view key_expanded
) {

    for (const YcMode& e : table) {
        const std::string_view k = e.key.view();
        if (infra::ieq(k, key) || infra::ieq(k, key_expanded))
            return e.phase_inc;
    }
    return std::nullopt;
}

VideoService::VideoService(Wiring w, adv7513::Io io) noexcept
    : hdmi_(io),
      sink_(w.sink),
      sampler_(w.sampler) {}

VideoService VideoService::create(Wiring w, adv7513::Io io) {
    return VideoService(w, io);
}

VideoService VideoService::create() {
    return VideoService(Wiring{}, adv7513::Io{});
}

Ex<video_timing::ParsedMode> VideoService::parse_mode(std::string_view spec) const {

    const auto scanned = scan_mode(spec);
    if (!scanned)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const ModeSpec& s = *scanned;

    Modeline m{};
    m.rb = rb_byte(ReducedBlanking::On);

    if (auto flagged = apply_mode_flags(s, m); !flagged)
        return std::unexpected(flagged.error());

    const std::size_t cnt = s.integers.count;
    if (cnt == kArityPreset) {

        return video_timing::ParsedMode{video_timing::PresetPick{
            .index = PresetIndex::of(s.val[kTokenWidth]),
            .hpol = m.hpol,
            .vpol = m.vpol
        }};
    }

    if (cnt == kArityCvt) {

        const double rate = (s.rate != 0.0) ? s.rate : static_cast<double>(s.val[kTokenRate]);

        const bool hpol = m.hpol;
        const bool vpol = m.vpol;
        const Resolution active{s.val[kTokenWidth], s.val[kTokenHeight]};
        m = calculate_cvt(
            active,
            static_cast<float>(rate),
            reduced_blanking_of(m.rb).value_or(ReducedBlanking::On),
            supports_pr_
        );
        m.hpol = hpol;
        m.vpol = vpol;
        return video_timing::ParsedMode{video_timing::CustomMode{.mode = m}};
    }

    if (cnt >= kArityRaw) {

        m = video_timing::with_timings(
            m,
            std::span<const std::uint32_t, kModelineTimings>(
                s.val.data() + item(ModelineItem::Hact),
                kModelineTimings
            )
        );
        const PllBlock raw_pll = PllBlock::from_item_words(
            std::span<const std::uint32_t, PllBlock::kItemWords>(
                s.val.data() + item(ModelineItem::PllFirst),
                PllBlock::kItemWords
            )
        );
        if (cnt > item(ModelineItem::Hpol)) {
            m.hpol = (s.val[item(ModelineItem::Hpol)] != 0);
        }
        if (cnt > item(ModelineItem::Vpol)) {
            m.vpol = (s.val[item(ModelineItem::Vpol)] != 0);
        }
        if (cnt > item(ModelineItem::Vic)) {
            m.vic = static_cast<std::uint8_t>(s.val[item(ModelineItem::Vic)] & kTokenByteMask);
        }
        if (cnt > item(ModelineItem::Rb)) {
            m.rb = static_cast<std::uint8_t>(s.val[item(ModelineItem::Rb)] & kTokenByteMask);
        }
        if (cnt > item(ModelineItem::PixelRepeat)) {
            m.pixel_repeat = (s.val[item(ModelineItem::PixelRepeat)] != 0);
        }

        return video_timing::ParsedMode{video_timing::CustomMode{.mode = m, .raw_pll = raw_pll}};
    }

    if (cnt == kArityTimings || cnt == kArityTimingsPolarity) {
        m = video_timing::with_timings(
            m,
            std::span<const std::uint32_t, kModelineTimings>(s.val.data(), kModelineTimings)
        );

        m.fpix_mhz = static_cast<double>(s.val[kTokenPixelClockKhz]) / kKhzPerMhz;
        if (cnt == kArityTimingsPolarity) {

            m.hpol = (s.val[kTokenShortHpol] != 0);
            m.vpol = (s.val[kTokenShortVpol] != 0);
        }
        return video_timing::ParsedMode{video_timing::CustomMode{.mode = m}};
    }

    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(cnt)});
}

Ex<void> VideoService::apply(const Modeline& m, PllBlock pll, Modeline::WireOptions opt) {

    if (!sink_)
        return err(Errc::negotiation, ERR_SITE());

    if (m.fpix_mhz > 0.0) {
        auto p = pll::solve(m.fpix_mhz);
        if (!p) {
            return err(Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(m.fpix_mhz));
        }
        pll = pll::block(*p);
    }

    opt.vsync_align = opt.vsync_align && (m.fpix_mhz > 0.0);

    const std::array<std::uint16_t, 26> words = m.to_wire(pll, opt);
    return sink_->publish(
        proto::UioOp::SetVideo,
        std::span<const std::uint16_t>(words.data(), words.size())
    );
}

Ex<void> VideoService::poll_resolution() {

    if (!sampler_)
        return err(Errc::negotiation, ERR_SITE());

    auto s = sampler_->sample();
    if (!s)
        return err(s.error().code, ERR_SITE(), s.error().detail);

    const bool res_changed = (s->res != gate_.last_res);
    gate_.last_res = s->res;
    if (res_changed)
        gate_.vi_seen = true;

    const bool fb_changed = (s->fb_crc != gate_.last_fb_crc);
    gate_.last_fb_crc = s->fb_crc;

    gate_.last = *s;

    const bool changed = gate_.vi_seen && (res_changed || fb_changed);
    if (changed)
        res_gen_ = res_gen_.next();

    return {};
}

}
