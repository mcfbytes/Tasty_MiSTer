// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra_doc.h"

#include "cores/stock_scan.h"
#include "infra/ascii.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::cores::mra {

namespace xml = svc::xml;
using infra::ieq;

std::vector<std::uint8_t> hex_bytes(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 2);
    const auto nib = [](char c) noexcept {
        return (static_cast<unsigned char>(c) % 32u + 9u) % 25u;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size()
               && (text[i] == '\n'
                   || text[i] == '\r'
                   || text[i] == ' '
                   || text[i] == ','
                   || text[i] == '\t')) {
            ++i;
        }
        if (i >= text.size())
            break;
        const unsigned hi = nib(text[i]) * 16u;
        ++i;
        if (i >= text.size()) {
            out.push_back(static_cast<std::uint8_t>(nib(text[i - 1])));
            break;
        }
        out.push_back(static_cast<std::uint8_t>(hi + nib(text[i])));
        ++i;
    }
    return out;
}

namespace {

using stock_scan::is_c_space;
using stock_scan::Scan;
using stock_scan::scan_atoi;
using stock_scan::scan_ul;

struct BitsScan {
    int num = 0;
    int b = 0;
    int e = 0;
};

[[nodiscard]] BitsScan scan_bits(std::string_view s) noexcept {
    BitsScan r;
    std::size_t i = 0;

    struct Dec {
        bool ok = false;
        int v = 0;
    };

    const auto dec = [&]() noexcept {
        while (i < s.size() && is_c_space(s[i]))
            ++i;
        bool neg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            neg = (s[i] == '-');
            ++i;
        }
        if (i >= s.size() || s[i] < '0' || s[i] > '9')
            return Dec{};
        long long acc = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (acc < 1000000000ll)
                acc = acc * 10 + (s[i] - '0');
            ++i;
        }
        return Dec{true, static_cast<int>(neg ? -acc : acc)};
    };
    const Dec first = dec();
    if (!first.ok)
        return r;
    r.b = first.v;
    r.num = 1;
    if (i >= s.size() || s[i] != ',')
        return r;
    ++i;
    const Dec second = dec();
    if (!second.ok)
        return r;
    r.e = second.v;
    r.num = 2;
    return r;
}

[[nodiscard]] std::string decode(std::string_view v) {
    static constexpr struct {
        char ch;
        std::string_view ent;
    } kEnt[] = {
        {'<', "&lt;"},
        {'>', "&gt;"},
        {'"', "&quot;"},
        {'\'', "&apos;"},
        {'&', "&amp;"},
    };

    std::string out;
    out.reserve(v.size());
    std::size_t i = 0;
    while (i < v.size()) {
        if (v[i] != '&') {
            out.push_back(v[i]);
            ++i;
            continue;
        }
        bool hit = false;
        for (const auto& e : kEnt) {
            if (v.substr(i, e.ent.size()) == e.ent) {
                out.push_back(e.ch);
                i += e.ent.size();
                hit = true;
                break;
            }
        }
        if (!hit) {
            out.push_back(v[i]);
            ++i;
        }
    }
    return out;
}

[[nodiscard]] std::string capped(std::string s) {
    if (s.size() > kDipTextChars)
        s.resize(kDipTextChars);
    return s;
}

struct Walk {
    explicit Walk(MraDoc& doc) : d(doc) {}

    MraDoc& d;

    bool insiderom = false;
    bool insidesw = false;
    bool insidecheats = false;
    bool insetname = false;
    bool inrotation = false;
    bool foundsetname = false;
    bool foundrotation = false;
    bool predone = false;

    std::string acc;

    std::string partname;
    std::string partzip;
    std::int32_t offset = 0;
    std::int32_t length = -1;
    std::int32_t repeat = 1;
    std::uint32_t crc = 0;
    std::uint32_t map = 0;
    std::uint32_t map_digits = 0;
    std::int32_t patchaddr = 0;
    bool patch_xor = false;
    int ilv_input = 0;
    int ilv_output = 0;
    DipRow dip{};
    std::string cheatname;

    void start_resets() {
        acc.clear();
        partname.clear();
        cheatname.clear();
        offset = 0;
        length = -1;
        repeat = 1;
        crc = 0;
    }

    void end_common() {
        insetname = false;
        inrotation = false;
        if (foundsetname && foundrotation)
            predone = true;
    }

    void finish_cheat() {
        MraCheat c;
        c.name = cheatname;
        c.bytes = hex_bytes(acc);
        const std::size_t unit = d.cheat_geometry.unit;
        if (unit == 0 || c.bytes.size() % unit != 0 || c.bytes.size() > kCheatTableBytes) {
            ++d.cheats_dropped;
            return;
        }
        d.cheats.push_back(std::move(c));
    }

    void finish_dip() {
        for (int i = 0; i < dip.num && i < static_cast<int>(kMaxDipIds); ++i) {
            const std::size_t n = static_cast<std::size_t>(i);
            dip.val[n] = (dip.has_val ? dip.val[n] : static_cast<std::uint64_t>(i)) << dip.start;
        }
        if (d.switches.rows.size() < kMaxDipRows)
            d.switches.rows.push_back(std::move(dip));
        dip = DipRow{};
    }

    void on_start(const xml::Tag& tag, const xml::Attr* attrs) {
        const std::string_view name = tag.name;
        start_resets();

        int rom_index = 0;
        std::string rom_zips;
        std::string rom_md5;
        std::uint32_t rom_address = 0;

        const bool is_rom = ieq(name, "rom");
        const bool is_switches = ieq(name, "switches");
        const bool is_interleave = ieq(name, "interleave");
        const bool is_part = ieq(name, "part");
        const bool is_patch = ieq(name, "patch");
        const bool is_dip = ieq(name, "dip");
        const bool is_buttons = ieq(name, "buttons");
        const bool is_nvram = ieq(name, "nvram");
        const bool is_cheats = ieq(name, "cheats");

        std::int32_t cheat_size = 0;
        std::int32_t cheat_max = 0;

        if (is_rom) {
            insiderom = true;
            ilv_input = 0;
            ilv_output = 0;
            map = 0;
            map_digits = 0;
        } else if (is_switches) {
            insidesw = true;
            d.switches = SwitchTable{};
            dip = DipRow{};
        } else if (is_interleave) {
            ilv_input = 8;
            ilv_output = 0;
            map = 0;
            map_digits = 0;
        } else if (is_part) {
            partzip.clear();
            map = 0;
            map_digits = 0;
        } else if (is_patch) {
            patchaddr = 0;
            patch_xor = false;
        } else if (ieq(name, "setname")) {
            if (!predone) {
                insetname = true;
                foundsetname = true;
            }
        } else if (ieq(name, "rotation")) {
            if (!predone) {
                inrotation = true;
                foundrotation = true;
            }
        }

        for (std::size_t i = 0; i < tag.attr_count; ++i) {
            const std::string_view an = attrs[i].name;
            const std::string v = decode(attrs[i].value);
            if (is_rom) {
                if (ieq(an, "zip"))
                    rom_zips = v;
                if (ieq(an, "md5"))
                    rom_md5 = v;
                if (ieq(an, "index"))
                    rom_index = scan_atoi(v);
                if (ieq(an, "address"))
                    rom_address = scan_ul(v, 0).value;
            }
            if (is_buttons) {
                if (ieq(an, "names"))
                    d.buttons_names = v;
                if (ieq(an, "default"))
                    d.buttons_default = v;
            }
            if (insetname && ieq(an, "same_dir") && v == "1")
                d.setname_same_dir = true;

            if (insiderom) {
                if (is_interleave) {
                    if (ieq(an, "input"))
                        ilv_input = static_cast<int>(scan_ul(v, 0).value);
                    if (ieq(an, "output"))
                        ilv_output = static_cast<int>(scan_ul(v, 0).value);
                }
                if (is_part) {
                    if (ieq(an, "zip"))
                        partzip = v;
                    if (ieq(an, "name"))
                        partname = v;
                    if (ieq(an, "offset"))
                        offset = static_cast<std::int32_t>(scan_ul(v, 0).value);
                    if (ieq(an, "length"))
                        length = static_cast<std::int32_t>(scan_ul(v, 0).value);
                    if (ieq(an, "repeat"))
                        repeat = static_cast<std::int32_t>(scan_ul(v, 0).value);
                    if (ieq(an, "crc"))
                        crc = scan_ul(v, 16).value;
                    if (ieq(an, "map")) {
                        map = scan_ul(v, 16).value;
                        map_digits = static_cast<std::uint32_t>(v.size());
                    }
                }
                if (is_patch) {
                    if (ieq(an, "offset")) {
                        patchaddr = static_cast<std::int32_t>(scan_ul(v, 0).value);
                    }
                    if (ieq(an, "operation") && ieq(v, "xor"))
                        patch_xor = true;
                }
            } else if (insidesw) {
                if (is_switches && ieq(an, "default")) {
                    const std::vector<std::uint8_t> b = hex_bytes(v);
                    for (std::size_t k = 0; k < b.size() && k < 8; ++k) {
                        d.switches.dip_def |= static_cast<std::uint64_t>(b[k]) << (k * 8);
                    }
                }
                if (is_dip) {
                    if (ieq(an, "name"))
                        dip.name = capped(v);
                    if (ieq(an, "bits")) {
                        const BitsScan r = scan_bits(v);
                        int b = r.b;
                        int e = r.e;
                        const bool bad = r.num <= 0
                            || b < 0
                            || b > 63
                            || e < 0
                            || e > 63
                            || (r.num == 2 && e < b);
                        if (!bad) {
                            if (r.num == 1)
                                e = b;
                            std::uint64_t mask = 1;
                            for (int k = 0; k < e - b; ++k)
                                mask = (mask << 1) | 1;
                            dip.start = b;
                            dip.mask = mask << b;
                            dip.size = e - b + 1;
                        }
                    }
                    if (ieq(an, "ids")) {
                        dip.ids.clear();
                        std::string_view rest = v;
                        int n = 0;
                        while (!rest.empty() && n < static_cast<int>(kMaxDipIds)) {
                            const std::size_t comma = rest.find(',');
                            const std::size_t len = (comma == std::string_view::npos) ? rest.size()
                                                                                      : comma;
                            dip.ids.push_back(capped(std::string(rest.substr(0, len))));
                            rest.remove_prefix(len);
                            if (!rest.empty() && rest.front() == ',')
                                rest.remove_prefix(1);
                            ++n;
                        }
                        dip.num = n;
                    }
                    if (ieq(an, "values")) {
                        std::string_view rest = v;
                        int n = 0;
                        while (!rest.empty() && n < static_cast<int>(kMaxDipIds)) {
                            const Scan r = scan_ul(rest, 0);
                            if (r.used == 0)
                                break;
                            dip.val[static_cast<std::size_t>(n)] = r.value;
                            rest.remove_prefix(r.used);
                            while (!rest.empty() && (rest.front() == ' ' || rest.front() == ',')) {
                                rest.remove_prefix(1);
                            }
                            ++n;
                        }
                        dip.has_val = true;
                    }
                }
            } else if (insidecheats) {
                if (ieq(name, "cheat") && ieq(an, "name")) {
                    cheatname = v;
                }
            } else {
                if (is_nvram && ieq(an, "index")) {
                    d.nvram_index = static_cast<int>(scan_ul(v, 0).value);
                }
                if (is_nvram && ieq(an, "size")) {
                    d.nvram_size = static_cast<int>(scan_ul(v, 0).value);
                }
                if (is_cheats && ieq(an, "size")) {
                    cheat_size = static_cast<std::int32_t>(scan_ul(v, 0).value);
                }
                if (is_cheats && ieq(an, "max")) {
                    cheat_max = static_cast<std::int32_t>(scan_ul(v, 0).value);
                }
            }
        }

        if (is_rom) {
            Rom r;
            r.index = rom_index;
            r.zips = std::move(rom_zips);
            r.md5 = std::move(rom_md5);
            r.address = rom_address;
            d.roms.push_back(std::move(r));
        }
        if (is_cheats) {
            insidecheats = true;
            d.cheats.clear();
            d.cheats_dropped = 0;
            d.cheat_geometry = clamp_cheat_geometry(cheat_size, cheat_max);
        }
        if (is_interleave && insiderom) {
            RomNode n;
            n.kind = RomNodeKind::InterleaveStart;
            n.input = ilv_input;
            n.output = ilv_output;
            d.roms.back().nodes.push_back(std::move(n));
        }
    }

    void on_end(std::string_view name) {
        if (ieq(name, "nvram")) {

            d.nvrams.push_back(
                NvramDecl{
                    .index = d.nvram_index,
                    .size = d.nvram_size,
                    .after = d.roms.size() - (insiderom ? 1u : 0u)
                }
            );
        }
        if (ieq(name, "rom")) {
            if (insiderom)
                d.roms.back().closed = true;
            insiderom = false;
        }
        if (ieq(name, "part") && insiderom) {
            RomNode n;
            if (!partname.empty()) {
                n.kind = RomNodeKind::FilePart;
                n.name = partname;
                n.zips = partzip;
                n.crc = crc;
                n.offset = offset;
                n.length = length;
            } else {
                n.kind = RomNodeKind::TextPart;
                n.text = acc;
            }
            n.repeat = repeat;
            n.map = map;
            n.map_digits = map_digits;
            d.roms.back().nodes.push_back(std::move(n));
        }
        if (ieq(name, "patch") && insiderom) {
            RomNode n;
            n.kind = RomNodeKind::Patch;
            n.offset = patchaddr;
            n.xor_op = patch_xor;
            n.text = acc;
            d.roms.back().nodes.push_back(std::move(n));
        }
        if (insidecheats && ieq(name, "cheat"))
            finish_cheat();
        if (insidesw && ieq(name, "dip"))
            finish_dip();
        if (ieq(name, "switches"))
            insidesw = false;
        if (ieq(name, "cheats"))
            insidecheats = false;
        if (ieq(name, "interleave")) {
            if (insiderom) {
                RomNode n;
                n.kind = RomNodeKind::InterleaveEnd;
                d.roms.back().nodes.push_back(std::move(n));
            }
            ilv_input = 0;
            ilv_output = 0;
            map = 0;
            map_digits = 0;
        }
        end_common();
    }

    void on_text(std::string_view text) {
        acc.append(text);
        if (insetname)
            d.setname.assign(text);
        if (inrotation) {
            d.vertical = text.size() >= 8 && ieq(text.substr(0, 8), "vertical");
            d.rotation = proto::RotationDir::None;
            if (d.vertical) {
                const auto has = [&](std::string_view w) {
                    return text.find(w) != std::string_view::npos;
                };
                const bool ccw = has("ccw")
                    || has("CCW")
                    || has("counterclockwise")
                    || has("counter-clockwise");
                d.rotation = ccw ? proto::RotationDir::Ccw : proto::RotationDir::Cw;
            }
        }
    }

    void on_markup() {
        start_resets();
        end_common();
    }
};

}

std::string_view mame_root_of(const svc::Vfs& vfs, std::string_view manifest_rel) noexcept {
    return vfs.dir_exists("games/mame") ? std::string_view("games") : arcade_root_of(manifest_rel);
}

std::string_view arcade_root_of(std::string_view mra_path) noexcept {

    std::size_t seg = std::string_view::npos;
    if (!mra_path.empty() && mra_path.front() == '_') {
        seg = 0;
    } else {
        const std::size_t hit = mra_path.find("/_");
        if (hit != std::string_view::npos)
            seg = hit + 1;
    }
    if (seg == std::string_view::npos)
        return {};
    const std::size_t end = mra_path.find('/', seg);

    if (end == std::string_view::npos)
        return {};
    return mra_path.substr(0, end);
}

std::string sibling_name(std::string_view mra_path, std::string_view ext) {
    const std::size_t slash = mra_path.rfind('/');
    std::string name(slash == std::string_view::npos ? mra_path : mra_path.substr(slash + 1));

    for (std::size_t i = 0; i + 4 <= name.size(); ++i) {
        if (ieq(std::string_view(name).substr(i, 4), ".mra")) {
            name.resize(i);
            name.append(ext);
            break;
        }
    }
    return name;
}

void MraDoc::parse(std::string_view xml_text) {
    *this = MraDoc{};
    Walk w{*this};

    xml::XmlScanner scan{xml_text};
    xml::Attr attrs[xml::kMaxAttrs];
    while (const auto t = scan.next_token(attrs)) {
        switch (t->kind) {
            case xml::Token::Kind::Start:
                w.on_start(t->tag, attrs);
                if (t->tag.self_closing)
                    w.on_end(t->tag.name);
                break;
            case xml::Token::Kind::End:
                w.on_end(t->tag.name);
                break;
            case xml::Token::Kind::Text:
                w.on_text(t->text);
                break;
            case xml::Token::Kind::Markup:
                w.on_markup();
                break;
        }
    }
}

}
