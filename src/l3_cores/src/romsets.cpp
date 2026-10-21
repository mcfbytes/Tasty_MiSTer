// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/romsets.h"

#include "cores/stock_scan.h"
#include "infra/ascii.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::cores::neogeo {

namespace xml = svc::xml;
using infra::ieq;

namespace {

using stock_scan::scan_atoi;

[[nodiscard]] bool icontains(std::string_view hay, std::string_view needle) noexcept {
    if (needle.size() > hay.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (xml::lower(hay[i + j]) != xml::lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

[[nodiscard]] std::string capped(std::string_view v, std::size_t n) {
    return std::string(v.substr(0, n));
}

inline constexpr std::size_t kNameCap = 254;
inline constexpr std::size_t kPlainCap = 255;

}

void RomsetIndex::scan(std::string_view xml_text) {
    entries.clear();

    xml::XmlScanner scan{xml_text};
    xml::Attr attrs[xml::kMaxAttrs];
    while (const auto t = scan.next_token(attrs)) {
        if (t->kind != xml::Token::Kind::Start)
            continue;
        if (!ieq(t->tag.name, "romset"))
            continue;
        if (entries.size() >= kRomsetIndexMax)
            continue;

        RomsetEntry e;
        for (std::size_t i = 0; i < t->tag.attr_count; ++i) {
            if (!ieq(attrs[i].name, "name"))
                continue;
            const std::string_view v = attrs[i].value;
            e.name = (v.find(',') != std::string_view::npos)
                ? capped("," + std::string(v) + ",", kNameCap)
                : capped(v, kPlainCap);
            e.altname = "No name";
        }
        for (std::size_t i = 0; i < t->tag.attr_count; ++i) {
            if (ieq(attrs[i].name, "altname")) {
                e.altname = capped(attrs[i].value, kPlainCap);
            } else if (ieq(attrs[i].name, "hide")) {
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
            return RomsetAlias{.drop = true, .alias = std::nullopt};

        const bool first_member = !comma_form
            || (e.name.size() >= needle.size()
                && ieq(std::string_view(e.name).substr(0, needle.size()), needle));
        if (first_member)
            return RomsetAlias{.drop = false, .alias = e.altname};
        return RomsetAlias{.drop = false, .alias = e.altname + " (" + std::string(key) + ")"};
    }
    return {};
}

std::string sidecar_altname(std::string_view xml_text) {
    std::string out;
    xml::XmlScanner scan{xml_text};
    xml::Attr attrs[xml::kMaxAttrs];
    while (const auto t = scan.next_token(attrs)) {
        if (t->kind != xml::Token::Kind::Start)
            continue;
        if (!ieq(t->tag.name, "romset"))
            continue;
        for (std::size_t i = 0; i < t->tag.attr_count; ++i) {
            if (ieq(attrs[i].name, "name"))
                out = capped(attrs[i].value, kPlainCap);
        }
        for (std::size_t i = 0; i < t->tag.attr_count; ++i) {
            if (ieq(attrs[i].name, "altname"))
                out = capped(attrs[i].value, kPlainCap);
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
    FileClass file_class = FileClass::Raw;
    std::uint8_t file_index = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t file_size = 0;
};

void on_romset_start(
    LoadWalk& w,
    std::string_view romset,
    const xml::Tag& tag,
    const xml::Attr* attrs
) {
    w.file_cnt = 0;
    w.vromb_offset = 0;
    w.vrom_mirror = true;
    w.cfg = RomsetConfig{};
    w.force_pcm = false;
    if (!w.romsets_wrapper)
        w.in_correct = true;

    for (std::size_t i = 0; i < tag.attr_count; ++i) {
        const std::string_view an = attrs[i].name;
        const std::string_view av = attrs[i].value;
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
            w.vromb_offset = stock_scan::scan_ul(av, 0).value;
        } else if (ieq(an, "vrom_mirror")) {
            w.vrom_mirror = stock_scan::scan_ul(av, 0).value != 0;
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

struct FileTyping {
    std::uint8_t index = 0;
    FileClass cls = FileClass::Raw;
};

[[nodiscard]] FileTyping type_from_letter(char c) noexcept {
    switch (c) {
        case 'C':
            return {15, FileClass::Spr};
        case 'M':
            return {9, FileClass::Raw};
        case 'P':
            return {4, FileClass::Raw};
        case 'S':
            return {8, FileClass::Fix};
        case 'V':
            return {16, FileClass::Raw};
        default:
            return {};
    }
}

void on_file_start(LoadWalk& w, const xml::Tag& tag, const xml::Attr* attrs) {
    w.file_offset = 0;
    w.file_size = 0;
    w.file_class = FileClass::Raw;
    w.file_index = 0;
    w.file_name.clear();

    bool use_index = false;
    for (std::size_t i = 0; i < tag.attr_count; ++i) {
        if (ieq(attrs[i].name, "index"))
            use_index = true;
    }
    for (std::size_t i = 0; i < tag.attr_count; ++i) {
        const std::string_view an = attrs[i].name;
        const std::string_view av = attrs[i].value;
        if (ieq(an, "name"))
            w.file_name = std::string(av);
        if (use_index) {
            if (ieq(an, "index")) {
                w.file_index = static_cast<std::uint8_t>(scan_atoi(av));
                if (w.file_index >= 64 || w.file_index == 15) {
                    w.file_class = FileClass::Spr;
                } else if (w.file_index == 2 || w.file_index == 8) {
                    w.file_class = FileClass::Fix;
                } else if (w.file_index >= 48 && w.file_index <= 63 && !w.force_pcm) {
                    w.cfg.use_pcm = 0;
                }
            }
        } else if (ieq(an, "type") && !av.empty()) {
            const FileTyping ft = type_from_letter(av[0]);
            w.file_index = ft.index;
            w.file_class = ft.cls;
        }
        if (ieq(an, "offset"))
            w.file_offset = stock_scan::scan_l(av, 0).value;
        if (ieq(an, "size"))
            w.file_size = stock_scan::scan_l(av, 0).value;
    }
    w.in_file = true;
}

void on_file_end(LoadWalk& w) {
    if (w.in_file) {
        RomsetFile f;
        f.name = w.file_name;
        f.file_class = w.file_class;
        f.index = w.file_index;
        f.offset = w.file_offset;
        f.size = w.file_size;
        if (!w.vrom_mirror && w.file_index >= 16 && w.file_index < 64) {
            const std::uint32_t stride = (static_cast<std::uint32_t>(w.file_index) - 16u)
                * 0x80000u;
            f.expand = kVromSize - (stride & 0xFFFFFFu);
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
    xml::Attr attrs[xml::kMaxAttrs];
    while (const auto t = scan.next_token(attrs)) {
        if (t->kind == xml::Token::Kind::Start) {
            const std::string_view name = t->tag.name;
            if (ieq(name, "romsets"))
                w.romsets_wrapper = true;
            if (ieq(name, "romset"))
                on_romset_start(w, romset, t->tag, attrs);
            if (w.in_correct && ieq(name, "file"))
                on_file_start(w, t->tag, attrs);
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
    std::vector<RomsetFile> out{
        {"prom", FileClass::Raw, 4, 0, 0, 0},
        {"p1rom", FileClass::Raw, 4, 0, 0, 0},
        {"p2rom", FileClass::Raw, 6, 0, 0, 0},
        {"srom", FileClass::Fix, 8, 0, 0, 0},
        {"crom0", FileClass::Spr, 15, 0, 0, 0},
        {"m1rom", FileClass::Raw, 9, 0, 0, 0},
    };
    if (plan.vromb_offset != 0) {
        out.push_back({"vroma0", FileClass::Raw, 16, 0, plan.vromb_offset, expand});
        out.push_back({"vroma0", FileClass::Raw, 48, plan.vromb_offset, 0, expand});
    } else {
        out.push_back({"vroma0", FileClass::Raw, 16, 0, 0, expand});
        out.push_back({"vromb0", FileClass::Raw, 48, 0, 0, expand});
    }
    return out;
}

}
