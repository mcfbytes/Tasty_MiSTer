// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra_doc.h"

#include <tuple>
#include <utility>

#include "cores/stock_scan.h"
#include "hal/types.h"
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
using stock_scan::Radix;
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

enum class Element : std::uint8_t {
    None,
    Rom,
    Switches,
    Interleave,
    Part,
    Patch,
    Dip,
    Cheat,
    Buttons,
    Nvram,
    Cheats,
    Setname,
    Rotation,
};

[[nodiscard]] Element element_of(std::string_view name) noexcept {
    if (ieq(name, "rom"))
        return Element::Rom;
    if (ieq(name, "switches"))
        return Element::Switches;
    if (ieq(name, "interleave"))
        return Element::Interleave;
    if (ieq(name, "part"))
        return Element::Part;
    if (ieq(name, "patch"))
        return Element::Patch;
    if (ieq(name, "dip"))
        return Element::Dip;
    if (ieq(name, "cheat"))
        return Element::Cheat;
    if (ieq(name, "buttons"))
        return Element::Buttons;
    if (ieq(name, "nvram"))
        return Element::Nvram;
    if (ieq(name, "cheats"))
        return Element::Cheats;
    if (ieq(name, "setname"))
        return Element::Setname;
    if (ieq(name, "rotation"))
        return Element::Rotation;
    return Element::None;
}

struct RomAttrs {
    int index = 0;
    std::string zips;
    std::string md5;
    hal::FabricAddr address{};
};

struct CheatAttrs {
    std::int32_t size = 0;
    std::int32_t max = 0;
};

struct PendingNode {

    void reset() noexcept {
        part_name.clear();
        offset = 0;
        length = -1;
        repeat = 1;
        crc = svc::Crc32{};
    }

    std::string part_name;
    std::string part_zip;
    std::int32_t offset = 0;
    std::int32_t length = -1;
    std::int32_t repeat = 1;
    svc::Crc32 crc{};

    MapWord map{};
    std::int32_t patch_addr = 0;
    bool patch_xor = false;
    int ilv_input = 0;
    int ilv_output = 0;
};

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
    PendingNode node;
    DipRow dip{};
    std::string cheatname;

    void start_resets() {
        acc.clear();
        node.reset();
        cheatname.clear();
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
        const proto::FileSize body{c.bytes.size()};
        if (!d.cheat_geometry.unit.whole(body) || body > proto::FileSize{kCheatTableBytes}) {
            ++d.cheats_dropped;
            return;
        }
        d.cheats.push_back(std::move(c));
    }

    void finish_dip() {
        for (std::size_t n = 0; n < kMaxDipIds && std::cmp_less(n, dip.num); ++n) {
            dip.val[n] = (dip.has_val ? dip.val[n] : DipWord{n}).shifted(dip.start);
        }
        if (d.switches.rows.size() < kMaxDipRows)
            d.switches.rows.push_back(std::move(dip));
        dip = DipRow{};
    }

    void on_start(const xml::Tag& tag, const xml::Attr* attrs) {
        const Element el = element_of(tag.name);
        start_resets();
        RomAttrs rom;
        CheatAttrs geom;
        open_element(el);

        for (std::size_t i = 0; i < tag.attr_count; ++i) {
            const std::string_view an = attrs[i].name;
            const std::string v = decode(attrs[i].value);
            if (el == Element::Rom)
                read_rom_attr(an, v, rom);
            read_document_attr(el, an, v, geom);
            if (insiderom) {
                if (el == Element::Interleave)
                    read_interleave_attr(an, v);
                if (el == Element::Part)
                    read_part_attr(an, v);
                if (el == Element::Patch)
                    read_patch_attr(an, v);
            } else if (insidesw) {
                read_switches_attr(el, an, v);
            }
        }

        close_start(el, std::move(rom), geom);
    }

    void open_element(Element el) {
        switch (el) {
            case Element::Rom:
                insiderom = true;
                node.ilv_input = 0;
                node.ilv_output = 0;
                node.map = MapWord{};
                break;
            case Element::Switches:
                insidesw = true;
                d.switches = SwitchTable{};
                dip = DipRow{};
                break;
            case Element::Interleave:
                node.ilv_input = kInterleaveByteBits;
                node.ilv_output = 0;
                node.map = MapWord{};
                break;
            case Element::Part:
                node.part_zip.clear();
                node.map = MapWord{};
                break;
            case Element::Patch:
                node.patch_addr = 0;
                node.patch_xor = false;
                break;
            case Element::Setname:
                if (!predone) {
                    insetname = true;
                    foundsetname = true;
                }
                break;
            case Element::Rotation:
                if (!predone) {
                    inrotation = true;
                    foundrotation = true;
                }
                break;
            case Element::None:
            case Element::Dip:
            case Element::Cheat:
            case Element::Buttons:
            case Element::Nvram:
            case Element::Cheats:
                break;
        }
    }

    void read_rom_attr(std::string_view an, const std::string& v, RomAttrs& rom) {
        if (ieq(an, "zip"))
            rom.zips = v;
        if (ieq(an, "md5"))
            rom.md5 = v;
        if (ieq(an, "index"))
            rom.index = scan_atoi(v);
        if (ieq(an, "address"))
            rom.address = hal::FabricAddr{scan_ul(v, Radix::Auto).value};
    }

    void read_interleave_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "input"))
            node.ilv_input = static_cast<int>(scan_ul(v, Radix::Auto).value);
        if (ieq(an, "output"))
            node.ilv_output = static_cast<int>(scan_ul(v, Radix::Auto).value);
    }

    void read_part_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "zip"))
            node.part_zip = v;
        if (ieq(an, "name"))
            node.part_name = v;
        if (ieq(an, "offset")) {
            node.offset = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
        if (ieq(an, "length")) {
            node.length = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
        if (ieq(an, "repeat")) {
            node.repeat = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
        if (ieq(an, "crc"))
            node.crc = svc::Crc32{scan_ul(v, Radix::Hex).value};
        if (ieq(an, "map")) {
            node.map = MapWord{
                .value = scan_ul(v, Radix::Hex).value,
                .digits = static_cast<std::uint32_t>(v.size())
            };
        }
    }

    void read_patch_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "offset")) {
            node.patch_addr = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
        if (ieq(an, "operation") && ieq(v, "xor"))
            node.patch_xor = true;
    }

    void read_switches_attr(Element el, std::string_view an, const std::string& v) {
        if (el == Element::Switches && ieq(an, "default")) {
            const std::vector<std::uint8_t> b = hex_bytes(v);
            for (std::size_t k = 0; k < b.size() && k < std::tuple_size_v<DipWord::Bytes>; ++k) {
                d.switches.dip_def = d.switches.dip_def.with_byte(k, b[k]);
            }
        }
        if (el != Element::Dip)
            return;
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
                dip.start = b;
                dip.mask = DipWord::ones(e - b + 1).shifted(b);
                dip.size = e - b + 1;
            }
        }
        if (ieq(an, "ids")) {
            dip.ids.clear();
            std::string_view rest = v;
            int n = 0;
            while (!rest.empty() && n < static_cast<int>(kMaxDipIds)) {
                const std::size_t comma = rest.find(',');
                const std::size_t len = (comma == std::string_view::npos) ? rest.size() : comma;
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
            std::size_t n = 0;
            while (!rest.empty() && n < kMaxDipIds) {
                const Scan r = scan_ul(rest, Radix::Auto);
                if (r.used == 0)
                    break;
                dip.val[n] = DipWord{r.value};
                rest.remove_prefix(r.used);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == ',')) {
                    rest.remove_prefix(1);
                }
                ++n;
            }
            dip.has_val = true;
        }
    }

    void read_document_attr(
        Element el,
        std::string_view an,
        const std::string& v,
        CheatAttrs& geom
    ) {
        if (el == Element::Buttons) {
            if (ieq(an, "names"))
                d.buttons_names = v;
            if (ieq(an, "default"))
                d.buttons_default = v;
        }
        if (insetname && ieq(an, "same_dir") && v == "1")
            d.setname_same_dir = true;

        if (insiderom || insidesw)
            return;
        if (insidecheats) {
            if (el == Element::Cheat && ieq(an, "name"))
                cheatname = v;
            return;
        }
        if (el == Element::Nvram && ieq(an, "index")) {
            d.nvram_index = static_cast<int>(scan_ul(v, Radix::Auto).value);
        }
        if (el == Element::Nvram && ieq(an, "size")) {
            d.nvram_size = static_cast<int>(scan_ul(v, Radix::Auto).value);
        }
        if (el == Element::Cheats && ieq(an, "size")) {
            geom.size = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
        if (el == Element::Cheats && ieq(an, "max")) {
            geom.max = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        }
    }

    void close_start(Element el, RomAttrs&& rom, const CheatAttrs& geom) {
        if (el == Element::Rom) {
            Rom r;
            r.index = rom.index;
            r.zips = std::move(rom.zips);
            r.md5 = std::move(rom.md5);
            r.address = rom.address;
            d.roms.push_back(std::move(r));
        }
        if (el == Element::Cheats) {
            insidecheats = true;
            d.cheats.clear();
            d.cheats_dropped = 0;
            d.cheat_geometry = clamp_cheat_geometry(geom.size, geom.max);
        }
        if (el == Element::Interleave && insiderom) {
            RomNode n;
            n.kind = RomNodeKind::InterleaveStart;
            n.input = node.ilv_input;
            n.output = node.ilv_output;
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
            if (!node.part_name.empty()) {
                n.kind = RomNodeKind::FilePart;
                n.name = node.part_name;
                n.zips = node.part_zip;
                n.crc = node.crc;
                n.offset = node.offset;
                n.length = node.length;
            } else {
                n.kind = RomNodeKind::TextPart;
                n.text = acc;
            }
            n.repeat = node.repeat;
            n.map = node.map;
            d.roms.back().nodes.push_back(std::move(n));
        }
        if (ieq(name, "patch") && insiderom) {
            RomNode n;
            n.kind = RomNodeKind::Patch;
            n.offset = node.patch_addr;
            n.xor_op = node.patch_xor;
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
            node.ilv_input = 0;
            node.ilv_output = 0;
            node.map = MapWord{};
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
