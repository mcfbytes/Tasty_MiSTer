// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str.h"

#include <cctype>
#include <charconv>
#include <optional>

namespace mister::proto {

namespace {

bool parse_num(std::string_view s, unsigned base, std::uint32_t& out) {
    if (s.starts_with("0x") || s.starts_with("0X")) {
        s.remove_prefix(2);
        base = 16;
    }
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out, static_cast<int>(base));
    return ec == std::errc{} && ptr == last && !s.empty();
}

bool starts_with_icase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto a = static_cast<unsigned char>(s[i]);
        const auto b = static_cast<unsigned char>(prefix[i]);
        if (std::toupper(a) != std::toupper(b)) return false;
    }
    return true;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::optional<reactor::Cause> cause_from(std::string_view name) {
    using reactor::Cause;
    if (name == "BLK") return Cause::Blk;
    if (name == "FIFO") return Cause::Fifo;
    if (name == "RING") return Cause::Ring;
    if (name == "TICK") return Cause::CoreTick;
    return std::nullopt;
}

std::uint32_t scan_hex(std::string_view s, std::size_t& pos) {
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), value, 16);
    if (ec != std::errc{} || ptr == s.data() + pos) return 0;
    pos = static_cast<std::size_t>(ptr - s.data());
    return value;
}

void scan_speed_list(std::string_view s, std::size_t& pos) {
    for (int entry = 0; entry < 10 && pos < s.size(); ++entry) {
        std::uint32_t speed = 0;

        const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), speed, 10);
        (void)ec;
        pos = static_cast<std::size_t>(ptr - s.data());
        if (pos < s.size() && s[pos] == '(') {
            ++pos;

            while (pos < s.size() && s[pos] != ';' && s[pos] != ':' && s[pos] != ')' &&
                   s[pos] != ',') {
                ++pos;
            }
            if (pos < s.size() && s[pos] == ')') ++pos;
        }
        if (pos < s.size() && s[pos] == ':') ++pos;
    }
}

}  // namespace

namespace {

bool scan_u32(std::string_view s, std::size_t& pos, std::uint32_t& out) {
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
        if (acc <= 0xFFFF'FFFFull) {
            acc = acc * 10u + static_cast<std::uint64_t>(s[p] - '0');
        }
        ++p;
    }
    if (p == first_digit) return false;
    if (acc > 0xFFFF'FFFFull) acc = 0xFFFF'FFFFull;
    std::uint32_t v = static_cast<std::uint32_t>(acc);
    if (neg) v = 0u - v;
    out = v;
    pos = p;
    return true;
}

int base32_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'V') return c - 'A' + 10;
    return -1;
}

mister::proto::BitField decode_bits(std::string_view s, mister::proto::ExShift ex,
                                    bool single) noexcept {
    using mister::proto::BitField;
    using mister::proto::ExShift;
    using mister::proto::StatusBit;

    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool sng = single;

    if (!s.empty() && s[0] == '[') {
        bool ranged = false;
        if (!sng) {
            std::size_t pos = 1;
            std::uint32_t hi = 0;
            std::uint32_t lo = 0;
            if (scan_u32(s, pos, hi) && pos < s.size() && s[pos] == ':') {
                ++pos;
                if (scan_u32(s, pos, lo)) {
                    end = hi;
                    start = lo;
                    ranged = true;
                }
            }
        }
        if (ranged) {
            if (start > 127 || end > 127 || end <= start) return {};
        } else {

            std::size_t pos = 1;
            if (!scan_u32(s, pos, start)) return {};
            if (start > 127) return {};
            end = start;
        }
    } else {
        const int d0 = s.empty() ? -1 : base32_digit(s[0]);
        if (d0 < 0) return {};
        start = static_cast<std::uint32_t>(d0);
        const int d1 = (!sng && s.size() > 1) ? base32_digit(s[1]) : -1;
        if (d1 >= 0) {
            end = static_cast<std::uint32_t>(d1);
        } else {
            sng = true;
            end = start;
        }
        if (ex == ExShift::Plus32) {
            start += 32;
            end += 32;
        }
        if (start > 127 || end > 127 || (!sng && end <= start)) return {};
    }

    if (end - start > 8) return {};
    return BitField{StatusBit{static_cast<std::uint8_t>(start)},
                    static_cast<std::uint8_t>(1 + end - start)};
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
        case HeadMatch::PrefixIcase: {
            if (field.size() < r.head.size()) return false;
            for (std::size_t i = 0; i < r.head.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(field[i])) !=
                    std::toupper(static_cast<unsigned char>(r.head[i]))) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

const mister::proto::ItemRule* match_rule(std::string_view field,
                                          mister::proto::MatchPhase phase) noexcept {
    for (const mister::proto::ItemRule& r : mister::proto::kItemRules) {
        if (r.phase != phase) continue;
        if (head_matches(field, r)) return &r;
    }
    return nullptr;
}

char at(std::string_view s, std::size_t i) noexcept { return i < s.size() ? s[i] : '\0'; }

}  // namespace

BitField decode_bit_ref(std::string_view spec, ExShift ex) noexcept {
    return decode_bits(spec, ex, false);
}

std::uint8_t decode_hd_selector(std::string_view after_letter) noexcept {
    const BitField b = decode_bits(after_letter, ExShift::None, true);
    return b.valid() ? b.start.v : 0;
}

Visibility evaluate(const Item& it, OsdMask hdmask) noexcept {
    Visibility v;
    for (std::uint8_t i = 0; i < it.cond_count && i < kMaxConds; ++i) {
        const Cond& c = it.conds[i];

        const bool set = c.bit < 16 && ((static_cast<unsigned>(hdmask.v) >> c.bit) & 1u) != 0u;
        if (set != c.acts_when_set) continue;
        if (c.effect == CondEffect::Hide)
            v.hidden = true;
        else
            v.disabled = true;
    }
    return v;
}

std::string_view ItemTable::subfield(const Item& it, unsigned n) const noexcept {
    if (n >= it.subfield_count) return {};
    const std::size_t idx = static_cast<std::size_t>(it.subfield_first) + n;
    if (idx >= spans_.size()) return {};
    return text(spans_[idx]);
}

Ex<ItemTable> ItemTable::parse(std::string_view raw) {
    ItemTable t;

    if (raw.size() > 0xFFFFu) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    if (!raw.empty()) t.text_.assign(raw.data(), raw.size());
    const std::string_view all{t.text_};

    const auto fields = split(all, ';');
    const auto span_of = [&](std::string_view v) {
        return TextSpan{static_cast<std::uint16_t>(v.data() - all.data()),
                        static_cast<std::uint16_t>(v.size())};
    };
    t.name_ = fields.empty() ? TextSpan{} : span_of(fields[0]);

    for (std::size_t i = 2; i < fields.size(); ++i) {
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

            while (p.size() > 2) {
                const CondRule* cr = nullptr;
                for (const CondRule& c : kCondRules) {
                    if (c.letter == p[0]) {
                        cr = &c;
                        break;
                    }
                }
                if (!cr) break;
                if (it.cond_count < kMaxConds) {
                    it.conds[it.cond_count++] =
                        Cond{cr->effect, cr->acts_when_set, decode_hd_selector(p.substr(1))};
                } else {
                    ++t.cond_overflows_;
                }
                p.remove_prefix(2);
            }

            if (!p.empty() && p[0] == 'P') {
                const int n = at(p, 1) - '0';

                const auto page_no = static_cast<std::uint8_t>(static_cast<unsigned>(n) & 0xFFu);
                if (at(p, 2) != ',') {
                    it.page = page_no;
                    p.remove_prefix(p.size() >= 2 ? 2 : p.size());
                } else {
                    it.declares_page = true;
                    it.declared_page = page_no;
                }
            }

            if (const ItemRule* r = match_rule(p, MatchPhase::AfterPrefixes)) {
                it.kind = r->kind;
                it.ex = r->ex;
            }
        }
        it.body = span_of(p);

        const auto subs = split(p, ',');

        if (subs.size() > 255 || t.spans_.size() + subs.size() > 0xFFFFu) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 1});
        }
        it.subfield_first = static_cast<std::uint16_t>(t.spans_.size());
        it.subfield_count = static_cast<std::uint8_t>(subs.size());
        for (std::string_view s : subs)
            t.spans_.push_back(span_of(s));

        switch (it.kind) {
            case ItemKind::Option:

                it.hps_marker = (at(p, 1) == 'X');
                {
                    const std::size_t off = it.hps_marker ? 2u : 1u;
                    it.bits = decode_bit_ref(p.substr(off < p.size() ? off : p.size()),
                                             it.hps_marker ? ExShift::None : it.ex);
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
                if (d >= '0' && d <= '9') {
                    it.digit = static_cast<std::uint8_t>(d - '0');
                    it.has_digit = true;
                }
                break;
            }
            default:
                break;
        }
        t.items_.push_back(it);
    }
    return t;
}

Ex<ConfStr> ConfStr::parse(std::string_view raw, hal::PhysRegion aperture) {
    ConfStr c;
    const auto fields = split(raw, ';');
    if (fields.empty()) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    c.name_ = std::string(fields[0]);

    auto try_ss = [&c, aperture](std::string_view f1, std::size_t& pos) -> bool {
        if (!starts_with_icase(f1.substr(pos), "SS")) return false;
        std::size_t p = pos + 2;
        const std::uint32_t base = scan_hex(f1, p);
        std::uint32_t size = 0;

        std::size_t next = p;
        if (p < f1.size() && f1[p] == ':') {
            ++p;
            next = p;
            size = scan_hex(f1, p);
        }

        const std::optional<hal::FabricAddr> at = hal::to_fabric(aperture.phys);
        const std::uint32_t low = at ? at->v : 0u;
        const auto end = static_cast<std::uint32_t>(low + aperture.len);
        const bool valid = at.has_value() && size > 0 && size <= (128u * 1024u * 1024u) &&
                           base >= low && base < end && (base + size) < end;
        c.ss_ = valid ? std::optional<SaveStateDecl>(SaveStateDecl{hal::FabricAddr{base}, size})
                      : std::nullopt;
        c.caps_.push_back({Capability::Kind::SaveState, std::string(f1.substr(pos, p - pos))});
        pos = next;
        return true;
    };
    auto try_uart = [&c](std::string_view f1, std::size_t& pos) -> bool {
        if (!starts_with_icase(f1.substr(pos), "UART")) return false;
        std::size_t p = pos + 4;
        scan_speed_list(f1, p);
        c.caps_.push_back({Capability::Kind::Uart, std::string(f1.substr(pos, p - pos))});
        pos = p;
        return true;
    };
    auto try_midi = [&c](std::string_view f1, std::size_t& pos) -> bool {
        if (!starts_with_icase(f1.substr(pos), "MIDI")) return false;
        std::size_t p = pos + 4;
        scan_speed_list(f1, p);
        c.caps_.push_back({Capability::Kind::Midi, std::string(f1.substr(pos, p - pos))});
        pos = p;
        return true;
    };

    auto try_irq = [&c](std::string_view f1, std::size_t& pos) -> Ex<bool> {
        if (!f1.substr(pos).starts_with("IRQ:")) return false;
        const std::size_t comma = f1.find(',', pos);
        const std::size_t seg_end = comma == std::string_view::npos ? f1.size() : comma;
        const std::string_view body = f1.substr(pos + 4, seg_end - (pos + 4));

        const std::size_t eq = body.find('=');
        if (eq == std::string_view::npos) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 1});
        }
        const auto klass = cause_from(body.substr(0, eq));
        std::string_view rest = body.substr(eq + 1);
        std::string_view line_s = rest;
        std::uint32_t cause_base = 0;
        if (const std::size_t colon = rest.find(':'); colon != std::string_view::npos) {
            line_s = rest.substr(0, colon);
            if (!parse_num(rest.substr(colon + 1), 16, cause_base)) {
                return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 2});
            }
        }

        std::uint32_t line = 0;
        if (!klass || !parse_num(line_s, 10, line) || line < kIrqPoolFirstLine ||
            line > kIrqOrdinalMax) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 3});
        }
        c.irq_.push_back(IrqBinding{
            *klass, static_cast<os::UioLine>(static_cast<std::uint8_t>(line - kIrqPoolFirstLine)),
            hal::LwOffset{cause_base}});
        c.caps_.push_back({Capability::Kind::Irq, std::string(f1.substr(pos, seg_end - pos))});
        pos = seg_end;
        return true;
    };
    auto try_axi = [&c](std::string_view f1, std::size_t& pos) -> Ex<bool> {
        if (!f1.substr(pos).starts_with("AXI:")) return false;
        const std::size_t comma = f1.find(',', pos);
        const std::size_t seg_end = comma == std::string_view::npos ? f1.size() : comma;
        const std::string_view body = f1.substr(pos + 4, seg_end - (pos + 4));

        const auto parts = split(body, ':');
        std::uint32_t addr = 0;
        std::uint32_t size = 0;

        if (parts.size() < 2 || parts.size() > 3 || !parse_num(parts[0], 16, addr) ||
            !parse_num(parts[1], 16, size)) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 4});
        }

        if (parts.size() == 2) {

            c.axi_unkinded_.push_back(AxiUnkindedDecl{AxiRawBase{addr}, size});
        } else if (parts[2] == "LW") {
            c.axi_lw_.push_back(AxiLwDecl{hal::LwOffset{addr}, size});
        } else if (parts[2] == "DDR") {
            c.axi_ddr_.push_back(AxiDdrDecl{hal::FabricAddr{addr}, size});
        } else {

            ++c.axi_unknown_kinds_;
        }
        c.caps_.push_back({Capability::Kind::Axi, std::string(f1.substr(pos, seg_end - pos))});
        pos = seg_end;
        return true;
    };

    if (fields.size() >= 2) {
        const std::string_view f1 = fields[1];
        std::size_t pos = 0;
        while (pos < f1.size()) {
            const std::size_t seg_start = pos;

            try_ss(f1, pos);
            try_uart(f1, pos);
            try_midi(f1, pos);
            if (auto r = try_irq(f1, pos); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = try_axi(f1, pos); !r) {
                return std::unexpected(r.error());
            }

            const std::size_t comma = f1.find(',', pos);
            if (pos == seg_start) {

                const std::size_t unk_end = comma == std::string_view::npos ? f1.size() : comma;
                if (unk_end > seg_start) {
                    c.caps_.push_back({Capability::Kind::Unknown,
                                       std::string(f1.substr(seg_start, unk_end - seg_start))});
                }
            }

            pos = comma == std::string_view::npos ? f1.size() : comma + 1;
        }
    }

    for (std::size_t i = 2; i < fields.size(); ++i) {
        c.items_.emplace_back(fields[i]);
    }
    auto ast = ItemTable::parse(raw);
    if (!ast) return std::unexpected(ast.error());
    c.ast_ = std::move(*ast);
    c.walk_items();
    return c;
}

std::string_view ConfStr::button_list(int type) const noexcept {
    for (const std::string& raw : items_) {
        const std::string_view p{raw};

        if (p.empty()) break;

        bool hit = false;
        if (type == 0) {
            hit = (p[0] == 'J');
        } else if (p.size() >= 2 && p[0] == 'j') {
            hit = (type == 1 && p[1] == 'n') || (type == 2 && p[1] == 'p');
        }
        if (!hit) continue;

        const std::size_t comma = p.find(',');
        if (comma == std::string_view::npos) break;
        const std::string_view tail = p.substr(comma + 1);
        if (tail.empty()) break;
        return tail;
    }
    return {};
}

namespace {

std::uint8_t view_page(const Item& it) noexcept { return it.declares_page ? 0 : it.page; }

const Item* bound_addon(std::span<const Item> items, std::size_t row) noexcept {
    const std::uint8_t page = view_page(items[row]);
    for (std::size_t i = row; i-- > 0;) {
        const Item& it = items[i];
        if (view_page(it) != page || evaluate(it, OsdMask{0}).hidden) continue;
        if (it.kind == ItemKind::Addon) return &it;
        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot) return nullptr;
    }
    return nullptr;
}

std::string_view addon_list(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr) return {};
    const std::string_view body = ast.text(addon->body);
    const std::size_t comma = body.find(',');
    return comma == std::string_view::npos ? std::string_view{} : body.substr(comma + 1);
}

bool addon_after(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr) return false;
    const std::string_view body = ast.text(addon->body);
    return body.size() >= 2 && body[1] == '1';
}

}  // namespace

void ConfStr::walk_items() { slots_ = slots_of(ast_); }

std::vector<ConfStrEntry> ConfStr::slots_of(const ItemTable& ast) {
    std::vector<ConfStrEntry> slots;
    std::uint32_t selentry = 0;
    const std::span<const Item> items = ast.items();
    for (std::size_t n = 0; n < items.size(); ++n) {
        const Item& it = items[n];
        if (evaluate(it, OsdMask{0}).hidden) continue;

        if (!it.declares_page && it.page != 0) continue;

        const Selectable sel = selectable_of(it.kind);
        if (sel == Selectable::Never) continue;

        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot) {
            ConfStrEntry fs{};
            fs.letter = (it.kind == ItemKind::FileSlot) ? 'F' : 'S';
            fs.selentry = static_cast<std::uint8_t>(selentry);
            fs.digit = it.digit;
            fs.has_digit = it.has_digit;
            fs.opensave = it.opensave;
            fs.store_name = it.store_name;

            if (const std::string_view ext = ast.subfield(it, 1); !ext.empty()) {
                fs.ext.assign(ext.data(), ext.size());
            }

            fs.ioctl_index = fs.has_digit         ? fs.digit
                             : (fs.letter == 'F') ? static_cast<std::uint8_t>(selentry + 1u)
                                                  : static_cast<std::uint8_t>(0u);
            if (fs.letter == 'F') fs.load_addr = parse_load_addr(ast.subfield(it, 3));
            const Item* addon = bound_addon(items, n);
            fs.addon = std::string{addon_list(ast, addon)};
            fs.addon_after = addon_after(ast, addon);
            slots.push_back(std::move(fs));
        }

        ++selentry;
    }
    return slots;
}

std::uint32_t parse_load_addr(std::string_view field) noexcept {
    std::size_t i = 0;
    while (i < field.size() && (field[i] == ' ' || (field[i] >= '\t' && field[i] <= '\r')))
        ++i;
    bool negate = false;
    if (i < field.size() && (field[i] == '+' || field[i] == '-')) negate = field[i++] == '-';
    if (i + 1 < field.size() && field[i] == '0' && (field[i + 1] == 'x' || field[i + 1] == 'X'))
        i += 2;
    std::uint64_t v = 0;
    for (; i < field.size(); ++i) {
        const char c = field[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = static_cast<unsigned>(c - 'A' + 10);
        else
            break;
        v = (v << 4) | d;
        if (v > 0xFFFF'FFFFu) return 0xFFFF'FFFFu;
    }
    const auto u = static_cast<std::uint32_t>(v);
    return negate ? static_cast<std::uint32_t>(0u - u) : u;
}

FileSlotHit ConfStr::find_file_slot(char type, std::uint8_t index) const noexcept {
    return find_slot_in(slots_, type, index);
}

FileSlotHit ConfStr::find_slot_in(std::span<const ConfStrEntry> slots, char type,
                                  std::uint8_t index) noexcept {
    const char want = (type >= 'a' && type <= 'z') ? static_cast<char>(type - 0x20) : type;
    const ConfStrEntry* hit = nullptr;
    for (const ConfStrEntry& s : slots) {

        const std::uint8_t num = s.has_digit ? s.digit : 0u;
        if (s.letter == want && num == index) hit = &s;
    }
    if (hit != nullptr) return FileSlotHit{hit, FileSlotMatch::Exact};

    for (const ConfStrEntry& s : slots) {
        if (s.selentry == 0 && s.letter == want) return FileSlotHit{&s, FileSlotMatch::RowZero};
    }
    return FileSlotHit{};
}

FileSlotHit ConfStr::find_load_slot(FileSlotDigit digit) const noexcept {
    return find_file_slot('F', digit.v);
}

std::optional<ConfStrFileRow> ConfStr::menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept {
    const auto items = ast_.items();
    if (item.v >= items.size()) return std::nullopt;
    const Item& it = items[item.v];
    if (it.kind != ItemKind::FileSlot) return std::nullopt;

    const Item* addon = bound_addon(items, item.v);
    return ConfStrFileRow{ast_.subfield(it, 1),
                          drawn.v,
                          parse_load_addr(ast_.subfield(it, 3)),
                          it.opensave,
                          addon_list(ast_, addon),
                          addon_after(ast_, addon)};
}

std::vector<ConfStrFileRow> ConfStr::remembered_rows() const {
    std::vector<ConfStrFileRow> rows;
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::FileSlot || !it.store_name || !it.has_digit) continue;
        rows.push_back(ConfStrFileRow{ast_.subfield(it, 1), it.digit,
                                      parse_load_addr(ast_.subfield(it, 3)), it.opensave});
    }
    return rows;
}

std::vector<IoIndex> ConfStr::remembered_mounts() const {
    std::vector<IoIndex> slots;
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::MountSlot || !it.store_name || !it.has_digit) continue;
        slots.push_back(IoIndex{it.digit});
    }
    return slots;
}

std::uint8_t ConfStr::ext_subindex(std::string_view filename, std::string_view ext_list) noexcept {
    const std::size_t dot = filename.rfind('.');
    if (dot == std::string_view::npos) return 0;

    char e[3] = {' ', ' ', ' '};
    for (std::size_t i = 0; i < 3 && dot + 1 + i < filename.size(); ++i) {
        e[i] = filename[dot + 1 + i];
    }
    std::uint8_t idx = 0;
    std::string_view rest = ext_list;
    while (!rest.empty()) {
        bool found = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const char c = i < rest.size() ? rest[i] : ' ';
            if (c == '*') break;
            if (c == '?') continue;
            const auto a = static_cast<unsigned char>(c);
            const auto b = static_cast<unsigned char>(e[i]);
            if (std::toupper(a) != std::toupper(b)) {
                found = false;
                break;
            }
        }
        if (found) return idx;
        if (rest.size() <= 3) break;
        ++idx;
        rest.remove_prefix(3);
    }
    return 0;
}

std::optional<os::UioLine> ConfStr::irq_line_for(reactor::Cause klass) const {
    for (const IrqBinding& b : irq_) {
        if (b.klass == klass) return b.line;
    }
    return std::nullopt;
}

}  // namespace mister::proto
