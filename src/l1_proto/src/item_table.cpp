// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/item_table.h"
#include "proto/item_rule.h"
#include "proto/status_word.h"
#include "conf_str_detail.h"

#include <cctype>
#include <limits>
#include <optional>

#include "infra/ascii.h"

namespace mister::proto {

namespace {

constexpr std::uint32_t kStatusBits = proto::StatusWord::kBits;

constexpr std::uint32_t kMaxFieldSpan = 8;

constexpr std::uint32_t kUpperBankFirstBit = 32;

constexpr std::uint32_t kMaxSpans = std::numeric_limits<std::uint16_t>::max();

constexpr std::size_t kMaxTextSpan = std::numeric_limits<std::uint16_t>::max();

constexpr std::size_t kMaxSubfields = std::numeric_limits<std::uint8_t>::max();

constexpr std::size_t kFirstItemField = 2;

[[nodiscard]] std::optional<Scan> scan_u32(std::string_view s, std::size_t pos) {
    std::size_t p = pos;
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
        ++p;
    bool neg = false;
    if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
        neg = (s[p] == '-');
        ++p;
    }
    const std::size_t first_digit = p;
    std::uint64_t acc = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
        if (acc <= std::numeric_limits<std::uint32_t>::max()) {
            acc = acc * 10u + static_cast<std::uint64_t>(s[p] - '0');
        }
        ++p;
    }
    if (p == first_digit)
        return std::nullopt;
    if (acc > std::numeric_limits<std::uint32_t>::max())
        acc = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t v = static_cast<std::uint32_t>(acc);
    if (neg)
        v = 0u - v;
    return Scan{v, p};
}

int base32_digit(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'V')
        return c - 'A' + 10;
    return -1;
}

mister::proto::BitField decode_bits(
    std::string_view s,
    mister::proto::ExShift ex,
    bool single
) noexcept {
    using mister::proto::BitField;
    using mister::proto::ExShift;
    using mister::proto::StatusBit;

    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool sng = single;

    if (!s.empty() && s[0] == '[') {
        bool ranged = false;
        if (!sng) {
            const auto hi = scan_u32(s, 1);
            if (hi && hi->pos < s.size() && s[hi->pos] == ':') {
                if (const auto lo = scan_u32(s, hi->pos + 1)) {
                    end = hi->value;
                    start = lo->value;
                    ranged = true;
                }
            }
        }
        if (ranged) {
            if (start >= kStatusBits || end >= kStatusBits || end <= start)
                return {};
        } else {

            const auto only = scan_u32(s, 1);
            if (!only)
                return {};
            start = only->value;
            if (start >= kStatusBits)
                return {};
            end = start;
        }
    } else {
        const int d0 = s.empty() ? -1 : base32_digit(s[0]);
        if (d0 < 0)
            return {};
        start = static_cast<std::uint32_t>(d0);
        const int d1 = (!sng && s.size() > 1) ? base32_digit(s[1]) : -1;
        if (d1 >= 0) {
            end = static_cast<std::uint32_t>(d1);
        } else {
            sng = true;
            end = start;
        }
        if (ex == ExShift::Plus32) {
            start += kUpperBankFirstBit;
            end += kUpperBankFirstBit;
        }
        if (start >= kStatusBits || end >= kStatusBits || (!sng && end <= start))
            return {};
    }

    if (end - start > kMaxFieldSpan)
        return {};
    return BitField{
        StatusBit{static_cast<std::uint8_t>(start)},
        static_cast<std::uint8_t>(1 + end - start)
    };
}

bool head_matches(std::string_view field, const mister::proto::ItemRule& r) noexcept {
    using mister::proto::HeadMatch;
    switch (r.match) {
        case HeadMatch::Letter:
            return !field.empty() && field[0] == r.letter;
        case HeadMatch::Exact:
            return field == r.head;
        case HeadMatch::Prefix:
            return field.starts_with(r.head);
        case HeadMatch::PrefixIcase:
            return mister::infra::istarts_with(field, r.head);
    }
    return false;
}

const mister::proto::ItemRule* match_rule(
    std::string_view field,
    mister::proto::MatchPhase phase
) noexcept {
    for (const mister::proto::ItemRule& r : mister::proto::kItemRules) {
        if (r.phase != phase)
            continue;
        if (head_matches(field, r))
            return &r;
    }
    return nullptr;
}

char at(std::string_view s, std::size_t i) noexcept {
    return i < s.size() ? s[i] : '\0';
}

struct PrefixDecode {
    std::string_view token;
    std::uint32_t cond_overflows = 0;
};

PrefixDecode decode_prefixes(std::string_view field, Item& it) {
    std::string_view p = field;
    std::uint32_t cond_overflows = 0;

    while (p.size() > 2) {
        const CondRule* cr = nullptr;
        for (const CondRule& c : kCondRules) {
            if (c.letter == p[0]) {
                cr = &c;
                break;
            }
        }
        if (!cr)
            break;
        if (it.cond_count < kMaxConds) {
            it.conds[it.cond_count++] = Cond{
                cr->effect,
                cr->acts_when_set,
                decode_hd_selector(p.substr(1))
            };
        } else {
            ++cond_overflows;
        }
        p.remove_prefix(2);
    }

    if (!p.empty() && p[0] == 'P') {
        const int n = at(p, 1) - '0';

        const SubmenuNo page_no{static_cast<std::uint8_t>(static_cast<unsigned>(n))};
        if (at(p, 2) != ',') {
            it.page = page_no;
            p.remove_prefix(p.size() >= 2 ? 2 : p.size());
        } else {
            it.declares_page = true;
            it.declared_page = page_no;
        }
    }
    return PrefixDecode{p, cond_overflows};
}

void decode_tail(std::string_view p, Item& it) noexcept {
    switch (it.kind) {
        case ItemKind::Option:

            it.hps_marker = (at(p, 1) == 'X');
            {
                const std::size_t off = it.hps_marker ? 2u : 1u;
                it.bits = decode_bit_ref(
                    p.substr(off < p.size() ? off : p.size()),
                    it.hps_marker ? ExShift::None : it.ex
                );
            }
            break;
        case ItemKind::Toggle:
        case ItemKind::ToggleClose:
            it.bits = decode_bit_ref(p.substr(p.empty() ? 0 : 1), it.ex);
            break;
        case ItemKind::FileSlot:
        case ItemKind::MountSlot: {

            std::size_t idx = 1;
            if (it.kind == ItemKind::FileSlot && at(p, idx) == 'S') {
                it.opensave = true;
                ++idx;
            }
            if (at(p, idx) == 'C') {
                it.store_name = true;
                ++idx;
            }
            const char d = at(p, idx);
            if (d >= '0' && d <= '9')
                it.digit = FileSlotDigit::from(static_cast<std::uint32_t>(d - '0'));
            break;
        }
        case ItemKind::Addon:
        case ItemKind::JoyNames:
        case ItemKind::Cheats:
        case ItemKind::Info:
        case ItemKind::Version:
        case ItemKind::CfgVer:
        case ItemKind::NoOsd:
        case ItemKind::PageDecl:
        case ItemKind::Separator:
        case ItemKind::Dip:
        case ItemKind::DefMra:
        case ItemKind::Turbo:
        case ItemKind::Unknown:
        case ItemKind::kCount:
            break;
    }
}

}

BitField decode_bit_ref(std::string_view spec, ExShift ex) noexcept {
    return decode_bits(spec, ex, false);
}

StatusBit decode_hd_selector(std::string_view after_letter) noexcept {
    const BitField b = decode_bits(after_letter, ExShift::None, true);
    return b.valid() ? b.start : StatusBit{};
}

Visibility evaluate(const Item& it, OsdMask hdmask) noexcept {
    Visibility v;
    for (std::uint8_t i = 0; i < it.cond_count && i < kMaxConds; ++i) {
        const Cond& c = it.conds[i];

        const bool set = hdmask.has(c.bit);
        if (set != c.acts_when_set)
            continue;
        if (c.effect == CondEffect::Hide)
            v.hidden = true;
        else
            v.disabled = true;
    }
    return v;
}

std::string_view ItemTable::subfield(const Item& it, unsigned n) const noexcept {
    if (n >= it.subfield_count)
        return {};
    const std::size_t idx = static_cast<std::size_t>(it.subfield_first) + n;
    if (idx >= spans_.size())
        return {};
    return text(spans_[idx]);
}

Ex<ItemTable> ItemTable::parse(std::string_view raw) {
    ItemTable t;

    if (raw.size() > kMaxTextSpan) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    if (!raw.empty())
        t.text_.assign(raw.data(), raw.size());
    const std::string_view all{t.text_};

    const auto fields = split(all, ';');
    const auto span_of = [&](std::string_view v) {
        return TextSpan{
            static_cast<std::uint16_t>(v.data() - all.data()),
            static_cast<std::uint16_t>(v.size())
        };
    };
    t.name_ = fields.empty() ? TextSpan{} : span_of(fields[0]);

    for (std::size_t i = kFirstItemField; i < fields.size(); ++i) {
        const std::string_view f = fields[i];

        if (f.empty()) {
            t.terminated_early_ = true;
            break;
        }

        Item it;
        it.raw = span_of(f);
        std::string_view p = f;

        if (const ItemRule* pre = match_rule(f, MatchPhase::BeforePrefixes)) {
            it.kind = pre->kind;
            it.ex = pre->ex;
        } else {
            const PrefixDecode decoded = decode_prefixes(f, it);
            p = decoded.token;
            t.cond_overflows_ += decoded.cond_overflows;
            if (const ItemRule* r = match_rule(p, MatchPhase::AfterPrefixes)) {
                it.kind = r->kind;
                it.ex = r->ex;
            }
        }
        it.body = span_of(p);

        const auto subs = split(p, ',');

        if (subs.size() > kMaxSubfields || t.spans_.size() + subs.size() > kMaxSpans) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 1});
        }
        it.subfield_first = static_cast<std::uint16_t>(t.spans_.size());
        it.subfield_count = static_cast<std::uint8_t>(subs.size());
        for (std::string_view s : subs)
            t.spans_.push_back(span_of(s));

        decode_tail(p, it);
        t.items_.push_back(it);
    }
    return t;
}

}
