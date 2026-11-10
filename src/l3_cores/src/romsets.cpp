// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/romset_plan.h"

#include "cores/stock_scan.h"
#include "cores/neogeo_file_index.h"
#include "cores/romset_alias.h"
#include "cores/romset_file.h"
#include "cores/romset_index.h"
#include "infra/ascii.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::cores::neogeo {

namespace xml = svc::xml;
using infra::icontains;
using infra::ieq;

namespace {

using stock_scan::Radix;
using stock_scan::scan_atoi;

[[nodiscard]] std::string capped(std::string_view v, std::size_t n) {
    return std::string(v.substr(0, n));
}

inline constexpr std::size_t kNameCap = 254;
inline constexpr std::size_t kPlainCap = 255;

}

void RomsetIndex::scan(std::string_view xml_text) {
    entries.clear();

    xml::XmlScanner scan{xml_text};
    while (const auto t = scan.next_token()) {
        if (t->kind != xml::Token::Kind::Start)
            continue;
        if (!ieq(t->tag.name, "romset"))
            continue;
        if (entries.size() >= kRomsetIndexMax)
            continue;

        RomsetEntry e;
        for (const xml::Attr& a : t->tag.attrs) {
            if (!ieq(a.name, "name"))
                continue;
            const std::string_view v = a.value;
            e.name = (v.find(',') != std::string_view::npos)
                ? capped("," + std::string(v) + ",", kNameCap)
                : capped(v, kPlainCap);
            e.altname = "No name";
        }
        for (const xml::Attr& a : t->tag.attrs) {
            if (ieq(a.name, "altname")) {
                e.altname = capped(a.value, kPlainCap);
            } else if (ieq(a.name, "hide")) {
                e.hide = true;
            }
        }
        entries.push_back(std::move(e));
    }
}

RomsetAlias RomsetIndex::lookup(std::string_view key) const {
    const std::string needle = "," + std::string(key) + ",";
    for (const RomsetEntry& e : entries) {
        const bool comma_form = !e.name.empty() && e.name[0] == ',';
        if (comma_form ? !icontains(e.name, needle) : !ieq(e.name, key))
            continue;
        if (e.hide)
            return RomsetDrop{};

        const bool first_member = !comma_form
            || (e.name.size() >= needle.size()
                && ieq(std::string_view(e.name).substr(0, needle.size()), needle));
        if (first_member)
            return RomsetRetype{e.altname};
        return RomsetRetype{e.altname + " (" + std::string(key) + ")"};
    }
    return RomsetNoMatch{};
}

std::string sidecar_altname(std::string_view xml_text) {
    std::string out;
    xml::XmlScanner scan{xml_text};
    while (const auto t = scan.next_token()) {
        if (t->kind != xml::Token::Kind::Start)
            continue;
        if (!ieq(t->tag.name, "romset"))
            continue;
        for (const xml::Attr& a : t->tag.attrs) {
            if (ieq(a.name, "name"))
                out = capped(a.value, kPlainCap);
        }
        for (const xml::Attr& a : t->tag.attrs) {
            if (ieq(a.name, "altname"))
                out = capped(a.value, kPlainCap);
        }
    }
    return out;
}

bool has_name(std::string_view nameset, std::string_view name) {
    if (nameset.find(',') == std::string_view::npos)
        return ieq(nameset, name);
    const std::string set = "," + std::string(nameset) + ",";
    const std::string needle = "," + std::string(name) + ",";
    return icontains(set, needle);
}

std::string get_romset(std::string_view path) {
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return {};
    std::string_view p = path.substr(slash + 1);
    std::size_t len = p.size();
    if (len > 4 && ieq(p.substr(len - 4), ".zip"))
        len -= 4;
    if (len > 31)
        len = 31;
    return std::string(p.substr(0, len));
}

namespace {

struct LoadWalk {
    bool romsets_wrapper = false;
    bool in_correct = false;
    bool in_file = false;
    bool force_pcm = false;

    RomsetPlan plan;
    RomsetConfig cfg;
    std::uint32_t vromb_offset = 0;
    bool vrom_mirror = true;
    int file_cnt = 0;

    std::string file_name;
    FileIndex file_index = FileIndex::Bios;
    std::uint32_t file_offset = 0;
    std::uint32_t file_size = 0;
};

void on_romset_start(LoadWalk& w, std::string_view romset, const xml::Tag& tag) {
    w.file_cnt = 0;
    w.vromb_offset = 0;
    w.vrom_mirror = true;
    w.cfg = RomsetConfig{};
    w.force_pcm = false;
    if (!w.romsets_wrapper)
        w.in_correct = true;

    for (const xml::Attr& a : tag.attrs) {
        const std::string_view an = a.name;
        const std::string_view av = a.value;
        if (w.romsets_wrapper && ieq(an, "name")) {
            w.in_correct = has_name(av, romset);
        } else if (ieq(an, "ct0")) {
            if (scan_atoi(av))
                w.cfg.hw_type = 1;
        } else if (ieq(an, "link")) {
            if (scan_atoi(av))
                w.cfg.hw_type = 2;
        } else if (ieq(an, "pvc")) {
            w.cfg.pvc = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "sma")) {
            w.cfg.sma = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "cmc")) {
            w.cfg.cmc = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "pcm")) {
            w.cfg.use_pcm = static_cast<std::uint32_t>(scan_atoi(av));
            w.force_pcm = true;
        } else if (ieq(an, "vromb_offset")) {
            w.vromb_offset = stock_scan::scan_ul(av, Radix::Auto).value;
        } else if (ieq(an, "vrom_mirror")) {
            w.vrom_mirror = stock_scan::scan_ul(av, Radix::Auto).value != 0;
        } else if (ieq(an, "rom_wait")) {
            w.cfg.rom_wait = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "p_wait")) {
            w.cfg.p_wait = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "xram")) {
            w.cfg.xram = static_cast<std::uint32_t>(scan_atoi(av));
        } else if (ieq(an, "ms5p")) {
            w.cfg.ms5p = static_cast<std::uint32_t>(scan_atoi(av));
        }
    }
}

[[nodiscard]] FileIndex type_from_letter(char c) noexcept {
    switch (c) {
        case 'C':
            return FileIndex::Sprite;
        case 'M':
            return FileIndex::M1;
        case 'P':
            return FileIndex::Prom;
        case 'S':
            return FileIndex::Srom;
        case 'V':
            return FileIndex::VromFirst;
        default:
            return FileIndex::Bios;
    }
}

void on_file_start(LoadWalk& w, const xml::Tag& tag) {
    w.file_offset = 0;
    w.file_size = 0;
    w.file_index = FileIndex::Bios;
    w.file_name.clear();

    bool use_index = false;
    for (const xml::Attr& a : tag.attrs) {
        if (ieq(a.name, "index"))
            use_index = true;
    }
    for (const xml::Attr& a : tag.attrs) {
        const std::string_view an = a.name;
        const std::string_view av = a.value;
        if (ieq(an, "name"))
            w.file_name = std::string(av);
        if (use_index) {
            if (ieq(an, "index")) {
                w.file_index = FileIndex::of(scan_atoi(av));
                if (w.file_index.in_pcm_range() && !w.force_pcm)
                    w.cfg.use_pcm = 0;
            }
        } else if (ieq(an, "type") && !av.empty()) {
            w.file_index = type_from_letter(av[0]);
        }
        if (ieq(an, "offset"))
            w.file_offset = stock_scan::scan_l(av, Radix::Auto).value;
        if (ieq(an, "size"))
            w.file_size = stock_scan::scan_l(av, Radix::Auto).value;
    }
    w.in_file = true;
}

void on_file_end(LoadWalk& w) {
    if (w.in_file) {
        RomsetFile f;
        f.name = w.file_name;
        f.index = w.file_index;
        f.offset = w.file_offset;
        f.size = w.file_size;
        if (!w.vrom_mirror && w.file_index.is_vrom()) {
            f.expand = kVromSize - w.file_index.mirror_stride();
        }
        w.plan.files.push_back(std::move(f));
        ++w.file_cnt;
    }
    w.in_file = false;
}

bool on_romset_end(LoadWalk& w) {
    if (w.file_cnt == 0) {
        w.plan.no_files_declared = true;
        if (w.vromb_offset != 0 && !w.force_pcm)
            w.cfg.use_pcm = 0;
    }
    w.plan.found = true;
    w.plan.config = w.cfg;
    w.plan.vromb_offset = w.vromb_offset;
    w.plan.vrom_mirror = w.vrom_mirror;
    w.plan.force_pcm = w.force_pcm;
    return false;
}

[[nodiscard]] bool handle_end(LoadWalk& w, std::string_view name) {
    if (!w.in_correct)
        return true;
    if (ieq(name, "romset"))
        return on_romset_end(w);
    if (ieq(name, "file"))
        on_file_end(w);
    return true;
}

}

RomsetPlan load_files(std::string_view xml_text, std::string_view romset) {
    LoadWalk w;

    xml::XmlScanner scan{xml_text};
    while (const auto t = scan.next_token()) {
        if (t->kind == xml::Token::Kind::Start) {
            const std::string_view name = t->tag.name;
            if (ieq(name, "romsets"))
                w.romsets_wrapper = true;
            if (ieq(name, "romset"))
                on_romset_start(w, romset, t->tag);
            if (w.in_correct && ieq(name, "file"))
                on_file_start(w, t->tag);
            if (t->tag.self_closing && !handle_end(w, name))
                break;
        } else if (t->kind == xml::Token::Kind::End) {
            if (!handle_end(w, t->tag.name))
                break;
        }
    }
    return w.plan;
}

std::vector<RomsetFile> fallback_files(const RomsetPlan& plan) {
    const std::uint32_t expand = plan.vrom_mirror ? 0u : kVromSize;
    const auto row = [](std::string name, FileIndex index) {
        return RomsetFile{.name = std::move(name), .index = index};
    };

    const auto vrom = [&](FileIndex index) {
        RomsetFile f = row("vroma0", index);
        f.expand = expand;
        return f;
    };
    std::vector<RomsetFile> out{
        row("prom", FileIndex::Prom),
        row("p1rom", FileIndex::Prom),
        row("p2rom", FileIndex::Prom2),
        row("srom", FileIndex::Srom),
        row("crom0", FileIndex::Sprite),
        row("m1rom", FileIndex::M1),
    };
    if (plan.vromb_offset != 0) {
        RomsetFile a = vrom(FileIndex::VromFirst);
        a.size = plan.vromb_offset;
        out.push_back(std::move(a));
        RomsetFile b = vrom(FileIndex::VromB);
        b.offset = plan.vromb_offset;
        out.push_back(std::move(b));
    } else {
        out.push_back(vrom(FileIndex::VromFirst));
        RomsetFile b = vrom(FileIndex::VromB);
        b.name = "vromb0";
        out.push_back(std::move(b));
    }
    return out;
}

}
