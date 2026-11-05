// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"
#include "svc/pll_params.h"

#include <cctype>
#include <charconv>
#include <cstring>

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

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y))
            return false;
    }
    return true;
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

}

std::string_view YcMode::key_view() const noexcept {
    std::size_t n = 0;
    while (n < key.size() && key[n] != '\0')
        ++n;
    return std::string_view(key.data(), n);
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

        const bool match = line.size() >= core_name.size()
            && iequals(line.substr(0, core_name.size()), core_name);
        if (match) {
            const std::size_t eq = line.find('=');

            if (eq != std::string_view::npos) {
                std::size_t v = eq + 1;
                while (v < line.size() && (line[v] == '=' || line[v] == ' ' || line[v] == '\t')) {
                    ++v;
                }
                YcMode e{};
                const std::string_view k = line.substr(0, eq);
                const std::size_t n = (k.size() < e.key.size() - 1) ? k.size() : e.key.size() - 1;
                std::memcpy(e.key.data(), k.data(), n);
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
        const std::string_view k = e.key_view();
        if (iequals(k, key) || iequals(k, key_expanded))
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
    return VideoService(Wiring{});
}

Ex<video_timing::ParsedMode> VideoService::parse_mode(std::string_view spec) const {

    constexpr std::size_t kTokPllFirst = 9;
    constexpr std::size_t kTokDocumentedForm = 21;
    constexpr std::size_t kTokHpol = 21;
    constexpr std::size_t kTokVpol = 22;
    constexpr std::size_t kTokVic = 23;
    constexpr std::size_t kTokRb = 24;
    constexpr std::size_t kTokPixelRepeat = 25;
    constexpr std::uint32_t kByteMask = 0xFF;
    constexpr std::size_t kTokPresetForm = 1;
    constexpr std::size_t kTokRetryForm = 2;
    constexpr std::size_t kTokTripleForm = 3;
    constexpr std::size_t kTokNineForm = 9;
    constexpr std::size_t kTokElevenForm = 11;
    constexpr std::size_t kTokPresetIndex = 0, kTokWidth = 0, kTokHeight = 1, kTokRate = 2;
    constexpr std::size_t kTokPixelClockKhz = 8;
    constexpr std::size_t kTokNineHpol = 9, kTokNineVpol = 10;
    constexpr double kKhzPerMhz = 1000.0;
    constexpr std::size_t kTimings = 8;
    if (spec.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const TokenList tokens = tokenize_commas(spec);
    const std::size_t token_cnt = tokens.count;
    if (token_cnt == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    std::array<std::uint32_t, kMaxTokens> val{};
    double valf = 0.0;
    std::size_t cnt = 0;
    for (; cnt < token_cnt; ++cnt) {
        const auto parsed = parse_u32_base0(tokens.items[cnt]);
        if (!parsed)
            break;
        val[cnt] = *parsed;
    }

    if (cnt == kTokRetryForm && token_cnt > kTokRetryForm) {
        if (const auto parsed = parse_double_all(tokens.items[cnt])) {
            valf = *parsed;
            ++cnt;
        }
    }

    video_timing::ParsedMode out{};
    Modeline& m = out.mode;
    m.rb = 1;

    for (std::size_t i = cnt; i < token_cnt; ++i) {
        const std::string_view flag = tokens.items[i];
        if (iequals(flag, "+vsync"))
            m.vpol = true;
        else if (iequals(flag, "-vsync"))
            m.vpol = false;
        else if (iequals(flag, "+hsync"))
            m.hpol = true;
        else if (iequals(flag, "-hsync"))
            m.hpol = false;
        else if (iequals(flag, "cvt"))
            m.rb = 0;
        else if (iequals(flag, "cvtrb"))
            m.rb = 1;
        else if (iequals(flag, "pr"))
            m.pixel_repeat = true;
        else {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)}
            );
        }
    }

    if (cnt == kTokPresetForm) {

        out.how = ModeRequest::PresetIndex;
        out.preset_index = val[kTokPresetIndex];
        return out;
    }

    if (cnt == kTokTripleForm) {

        const double rate = (valf != 0.0) ? valf : static_cast<double>(val[kTokRate]);

        const bool hpol = m.hpol;
        const bool vpol = m.vpol;
        out.mode = calculate_cvt(
            val[kTokWidth],
            val[kTokHeight],
            static_cast<float>(rate),
            m.rb,
            supports_pr_
        );
        out.mode.hpol = hpol;
        out.mode.vpol = vpol;
        out.how = ModeRequest::Triple;
        return out;
    }

    if (cnt >= kTokDocumentedForm) {

        video_timing::assign_timings(
            m,
            std::span<const std::uint32_t, kTimings>(val.data() + 1, kTimings)
        );
        PllBlock pll{};
        for (std::size_t i = 0; i < pll.size(); ++i)
            pll[i] = val[kTokPllFirst + i];
        out.raw_pll = pll;
        if (cnt > kTokHpol)
            m.hpol = (val[kTokHpol] != 0);
        if (cnt > kTokVpol)
            m.vpol = (val[kTokVpol] != 0);
        if (cnt > kTokVic)
            m.vic = static_cast<std::uint8_t>(val[kTokVic] & kByteMask);
        if (cnt > kTokRb)
            m.rb = static_cast<std::uint8_t>(val[kTokRb] & kByteMask);
        if (cnt > kTokPixelRepeat)
            m.pixel_repeat = (val[kTokPixelRepeat] != 0);

        out.how = ModeRequest::RawModeline;
        return out;
    }

    if (cnt == kTokNineForm || cnt == kTokElevenForm) {
        video_timing::assign_timings(
            m,
            std::span<const std::uint32_t, kTimings>(val.data(), kTimings)
        );

        m.fpix_mhz = static_cast<double>(val[kTokPixelClockKhz]) / kKhzPerMhz;
        if (cnt == kTokElevenForm) {

            m.hpol = (val[kTokNineHpol] != 0);
            m.vpol = (val[kTokNineVpol] != 0);
            out.how = ModeRequest::Eleven;
        } else {
            out.how = ModeRequest::Nine;
        }
        return out;
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

void VideoService::set_filter_slots(std::span<const bool> enabled) noexcept {
    for (std::size_t i = 0; i < filter_slots_.size(); ++i) {
        filter_slots_[i] = (i < enabled.size()) && enabled[i];
    }
}

Ex<void> VideoService::poll_resolution() {

    if (!sampler_)
        return err(Errc::negotiation, ERR_SITE());

    const bool force = force_next_;
    auto s = sampler_->sample(force);
    if (!s)
        return err(s.error().code, ERR_SITE(), s.error().detail);
    force_next_ = false;

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

    const bool flt_force = changed;
    if (s->flt_flags != 0 && (flt_force || gate_.last_flt_flags != s->flt_flags)) {
        gate_.last_flt_flags = s->flt_flags;
        vfilter_ = video_timing::select_vfilter(gate_.last, std::span<const bool>(filter_slots_));
    }

    return {};
}

}
