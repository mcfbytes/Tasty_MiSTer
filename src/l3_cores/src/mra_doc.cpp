// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra_doc.h"

#include <limits>
#include <tuple>
#include <utility>

#include "cores/arcade_facts.h"
#include "cores/interleave_widths.h"
#include "cores/stock_scan.h"
#include "hal/types.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"
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

std::optional<proto::WideIoIndex> rom_index_of(std::string_view digits) noexcept {
    const int n = stock_scan::scan_atoi(digits);
    if (n < 0 || n > std::numeric_limits<std::uint8_t>::max())
        return std::nullopt;
    return proto::WideIoIndex{static_cast<std::uint16_t>(n)};
}

std::optional<proto::WideIoIndex> nvram_index_of(std::string_view digits) noexcept {
    const std::uint32_t n = stock_scan::scan_ul(digits, stock_scan::Radix::Auto).value;
    if (n == 0 || n > std::numeric_limits<std::uint8_t>::max())
        return std::nullopt;
    return proto::WideIoIndex{static_cast<std::uint16_t>(n)};
}

std::optional<proto::FileSize> nvram_size_of(std::string_view digits) noexcept {
    const std::uint32_t n = stock_scan::scan_ul(digits, stock_scan::Radix::Auto).value;
    if (n == 0 || n > kNvramCap)
        return std::nullopt;
    return proto::FileSize{n};
}

namespace {

using stock_scan::is_c_space;
using stock_scan::Radix;
using stock_scan::Scan;
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
    std::optional<proto::WideIoIndex> index = proto::WideIoIndex{};
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
        offset = RomOffset{};
        length = std::nullopt;
        repeat = RepeatCount{1};
        crc = svc::Crc32{};
    }

    std::string part_name;
    std::string part_zip;
    std::optional<RomOffset> offset = RomOffset{};
    std::optional<RomLength> length{};
    RepeatCount repeat{1};
    svc::Crc32 crc{};

    MapWord map{};
    std::optional<RomOffset> patch_addr = RomOffset{};
    bool patch_xor = false;
    InterleaveWidths ilv{};
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
    std::string dip_values_text;
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

    void reset_dip() {
        dip = DipRow{};
        dip_values_text.clear();
    }

    void finish_dip() {
        std::string_view rest = dip_values_text;
        std::size_t n = 0;
        while (!rest.empty() && n < dip.choices.size()) {
            const Scan r = scan_ul(rest, Radix::Auto);
            if (r.used == 0)
                break;
            dip.choices[n].val = DipWord{r.value};
            rest.remove_prefix(r.used);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == ',')) {
                rest.remove_prefix(1);
            }
            ++n;
        }
        for (std::size_t i = 0; i < dip.choices.size(); ++i) {
            dip.choices[i].val = dip.field.place(dip.has_val ? dip.choices[i].val : DipWord{i});
        }
        if (d.switches.rows.size() < kMaxDipRows)
            d.switches.rows.push_back(std::move(dip));
        reset_dip();
    }

    void on_start(const xml::Tag& tag) {
        const Element el = element_of(tag.name);
        start_resets();
        RomAttrs rom;
        CheatAttrs geom;
        open_element(el);

        for (const xml::Attr& a : tag.attrs) {
            read_attr(el, a.name, decode(a.value), rom, geom);
        }

        close_start(el, std::move(rom), geom);
    }

    void read_attr(
        Element el,
        std::string_view an,
        const std::string& v,
        RomAttrs& rom,
        CheatAttrs& geom
    ) {

        if (insetname && ieq(an, "same_dir") && v == "1")
            d.setname_same_dir = true;

        const bool swallowed = insiderom || insidesw;
        switch (el) {
            case Element::Rom:
                read_rom_attr(an, v, rom);
                break;
            case Element::Switches:
                if (!insiderom)
                    read_switches_default(an, v);
                break;
            case Element::Interleave:
                if (insiderom)
                    read_interleave_attr(an, v);
                break;
            case Element::Part:
                if (insiderom)
                    read_part_attr(an, v);
                break;
            case Element::Patch:
                if (insiderom)
                    read_patch_attr(an, v);
                break;
            case Element::Dip:
                if (!insiderom && insidesw)
                    read_dip_attr(an, v);
                break;
            case Element::Cheat:
                if (!swallowed && insidecheats && ieq(an, "name"))
                    cheatname = v;
                break;
            case Element::Buttons:
                if (ieq(an, "names"))
                    d.buttons_names = v;
                if (ieq(an, "default"))
                    d.buttons_default = v;
                break;
            case Element::Nvram:
                if (!swallowed && !insidecheats)
                    read_nvram_attr(an, v);
                break;
            case Element::Cheats:
                if (!swallowed && !insidecheats)
                    read_cheats_attr(an, v, geom);
                break;
            case Element::Setname:
            case Element::Rotation:
            case Element::None:
                break;
        }
    }

    void open_element(Element el) {
        switch (el) {
            case Element::Rom:
                insiderom = true;
                node.ilv = InterleaveWidths{};
                node.map = MapWord{};
                break;
            case Element::Switches:
                insidesw = true;
                d.switches = SwitchTable{};
                reset_dip();
                break;
            case Element::Interleave:
                node.ilv = InterleaveWidths{.input = kInterleaveByteBits};
                node.map = MapWord{};
                break;
            case Element::Part:
                node.part_zip.clear();
                node.map = MapWord{};
                break;
            case Element::Patch:
                node.patch_addr = RomOffset{};
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
            rom.index = rom_index_of(v);
        if (ieq(an, "address"))
            rom.address = hal::FabricAddr{scan_ul(v, Radix::Auto).value};
    }

    void read_interleave_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "input"))
            node.ilv.input = BitWidth::of(scan_ul(v, Radix::Auto).value);
        if (ieq(an, "output"))
            node.ilv.output = BitWidth::of(scan_ul(v, Radix::Auto).value);
    }

    void read_part_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "zip"))
            node.part_zip = v;
        if (ieq(an, "name"))
            node.part_name = v;
        if (ieq(an, "offset")) {
            node.offset = RomOffset::of_attribute(
                static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value)
            );
        }
        if (ieq(an, "length")) {
            node.length = RomLength::of_attribute(
                static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value)
            );
        }
        if (ieq(an, "repeat")) {
            node.repeat = RepeatCount::of_attribute(
                              static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value)
            )
                              .value_or(RepeatCount{});
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
            node.patch_addr = RomOffset::of_attribute(
                static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value)
            );
        }
        if (ieq(an, "operation") && ieq(v, "xor"))
            node.patch_xor = true;
    }

    void read_switches_default(std::string_view an, const std::string& v) {
        if (!ieq(an, "default"))
            return;
        const std::vector<std::uint8_t> b = hex_bytes(v);
        for (std::size_t k = 0; k < b.size() && k < std::tuple_size_v<DipWord::Bytes>; ++k) {
            d.switches.dip_def = d.switches.dip_def.with_byte(k, b[k]);
        }
    }

    void read_dip_attr(std::string_view an, const std::string& v) {
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
                dip.field = DipField{.start = b, .width = e - b + 1};
            }
        }
        if (ieq(an, "ids")) {
            dip.choices.clear();
            std::string_view rest = v;
            while (!rest.empty() && dip.choices.size() < kMaxDipIds) {
                const std::size_t comma = rest.find(',');
                const std::size_t len = (comma == std::string_view::npos) ? rest.size() : comma;
                dip.choices.push_back(DipChoiceRow{.id = capped(std::string(rest.substr(0, len)))});
                rest.remove_prefix(len);
                if (!rest.empty() && rest.front() == ',')
                    rest.remove_prefix(1);
            }
        }
        if (ieq(an, "values")) {

            dip_values_text = v;
            dip.has_val = true;
        }
    }

    void read_nvram_attr(std::string_view an, const std::string& v) {
        if (ieq(an, "index"))
            d.nvram_index = nvram_index_of(v);
        if (ieq(an, "size"))
            d.nvram_size = nvram_size_of(v);
    }

    void read_cheats_attr(std::string_view an, const std::string& v, CheatAttrs& geom) {
        if (ieq(an, "size"))
            geom.size = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
        if (ieq(an, "max"))
            geom.max = static_cast<std::int32_t>(scan_ul(v, Radix::Auto).value);
    }

    void close_start(Element el, RomAttrs&& rom, const CheatAttrs& geom) {
        switch (el) {
            case Element::Rom: {
                Rom r;
                r.index = rom.index;
                r.zips = std::move(rom.zips);
                r.md5 = std::move(rom.md5);
                r.address = rom.address;
                d.roms.push_back(std::move(r));
                break;
            }
            case Element::Cheats:
                insidecheats = true;
                d.cheats.clear();
                d.cheats_dropped = 0;
                d.cheat_geometry = clamp_cheat_geometry(geom.size, geom.max);
                break;
            case Element::Interleave:
                if (insiderom) {
                    d.roms.back().nodes.emplace_back(InterleaveStart{.widths = node.ilv});
                }
                break;
            case Element::None:
            case Element::Switches:
            case Element::Part:
            case Element::Patch:
            case Element::Dip:
            case Element::Cheat:
            case Element::Buttons:
            case Element::Nvram:
            case Element::Setname:
            case Element::Rotation:
                break;
        }
    }

    void on_end(std::string_view name) {
        switch (element_of(name)) {
            case Element::Nvram:

                d.nvrams.push_back(
                    NvramDecl{
                        .save = d.nvram_save(),
                        .after = d.roms.size() - (insiderom ? 1u : 0u)
                    }
                );
                break;
            case Element::Rom:
                if (insiderom)
                    d.roms.back().closed = true;
                insiderom = false;
                break;
            case Element::Part:
                if (insiderom) {
                    const PartCommon common{.repeat = node.repeat, .map = node.map};
                    if (!node.part_name.empty()) {
                        d.roms.back().nodes.emplace_back(
                            FilePart{
                                .common = common,
                                .name = node.part_name,
                                .zips = node.part_zip,
                                .crc = node.crc,
                                .offset = node.offset,
                                .length = node.length,
                            }
                        );
                    } else {
                        d.roms.back().nodes.emplace_back(
                            TextPart{
                                .common = common,
                                .text = acc,
                            }
                        );
                    }
                }
                break;
            case Element::Patch:
                if (insiderom) {
                    d.roms.back().nodes.emplace_back(
                        Patch{
                            .text = acc,
                            .address = node.patch_addr,
                            .xor_op = node.patch_xor,
                        }
                    );
                }
                break;
            case Element::Cheat:
                if (insidecheats)
                    finish_cheat();
                break;
            case Element::Dip:
                if (insidesw)
                    finish_dip();
                break;
            case Element::Switches:
                insidesw = false;
                break;
            case Element::Cheats:
                insidecheats = false;
                break;
            case Element::Interleave:
                if (insiderom)
                    d.roms.back().nodes.emplace_back(InterleaveEnd{});
                node.ilv = InterleaveWidths{};
                node.map = MapWord{};
                break;
            case Element::None:
            case Element::Buttons:
            case Element::Setname:
            case Element::Rotation:
                break;
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
    std::string name(infra::basename(mra_path));

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
    while (const auto t = scan.next_token()) {
        switch (t->kind) {
            case xml::Token::Kind::Start:
                w.on_start(t->tag);
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
