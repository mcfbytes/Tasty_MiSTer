// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/zx81/zx81_sidecars.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

#include "cores/zx81/sidecar_class.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/asset_name.h"
#include "svc/file.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::cores::zx81 {

namespace {

namespace xml = svc::xml;
using infra::ieq;

constexpr std::string_view sibling_ext(SidecarClass which) noexcept {
    switch (which) {
        case SidecarClass::Colour:
            return ".col";
        case SidecarClass::Chars:
            return ".chr";
    }
    return {};
}

constexpr std::string_view kNoAttr = "-1";

std::string_view attr_text(std::span<const xml::Attr> attrs, std::string_view name) noexcept {
    std::string_view value = kNoAttr;
    for (const xml::Attr& a : attrs)
        if (ieq(a.name, name))
            value = a.value;
    return value;
}

bool only_space(std::string_view tail) noexcept {
    return std::ranges::all_of(tail, [](char c) noexcept { return xml::is_space(c); });
}

template <class OnStart, class OnEnd, class OnText>
bool walk(std::string_view doc, OnStart&& on_start, OnEnd&& on_end, OnText&& on_text) {
    xml::XmlScanner scan(doc);
    std::string_view tail = doc;
    while (const auto t = scan.next_token()) {
        tail = doc.substr(scan.pos());
        switch (t->kind) {
            case xml::Token::Kind::Start:
                on_start(t->tag);
                if (t->tag.self_closing)
                    on_end(t->tag.name);
                break;
            case xml::Token::Kind::End:
                on_end(t->tag.name);
                break;
            case xml::Token::Kind::Text:
                on_text(t->text);
                break;
            case xml::Token::Kind::Markup:
                break;
        }
    }
    return only_space(tail);
}

}

std::optional<ColourMap> Zx81Sidecars::colour_map(std::string_view doc) const {
    TASTY_SEAT_BODY(Zx81Sidecars);

    struct Entry {
        ColourCode first{};
        CodeCount count{};
        ColourMap::Cell cell{};
    };

    ColourMap map;
    std::optional<Entry> entry;
    std::optional<PixelLine> line;
    bool border = false, colour = false, bright = false, paper = false, ink = false;

    const auto on_start = [&](const xml::Tag& tag) {
        if (ieq(tag.name, "colourisation")) {
            entry.reset();
            line.reset();
            border = colour = bright = paper = ink = false;
        }
        if (ieq(tag.name, "border"))
            border = true;
        if (ieq(tag.name, "colour"))
            colour = true;
        if (ieq(tag.name, "bright"))
            bright = true;
        if (ieq(tag.name, "entry")) {
            entry.reset();
            if (const auto first = ColourCode::of_text(attr_text(tag.attrs, "code")))
                entry = Entry{
                    .first = *first,
                    .count = CodeCount::of_text(attr_text(tag.attrs, "quantity"))
                };
        }
        if (ieq(tag.name, "line")) {
            line = PixelLine::of_text(attr_text(tag.attrs, "index"));
            if (line && entry)
                entry->cell[*line] = ColourAttr{};
        }
        if (ieq(tag.name, "paper"))
            paper = true;
        if (ieq(tag.name, "ink"))
            ink = true;
    };
    const auto on_end = [&](std::string_view name) {
        if (ieq(name, "border"))
            border = false;
        if (ieq(name, "colour"))
            colour = false;
        if (ieq(name, "bright"))
            bright = false;
        if (ieq(name, "line"))
            line.reset();
        if (ieq(name, "paper"))
            paper = false;
        if (ieq(name, "ink"))
            ink = false;
        if (ieq(name, "entry")) {
            if (entry)
                map.paint(entry->first, entry->count, entry->cell);
            entry.reset();
        }
    };
    const auto on_text = [&](std::string_view text) {
        if (border && colour)
            map.set_border(map.border().with_ink(text));
        if (border && bright)
            map.set_border(map.border().with_ink_bright(text));
        if (!entry || !line)
            return;
        ColourAttr& attr = entry->cell[*line];
        if (ink && colour)
            attr = attr.with_ink(text);
        if (ink && bright)
            attr = attr.with_ink_bright(text);
        if (paper && colour)
            attr = attr.with_paper(text);
        if (paper && bright)
            attr = attr.with_paper_bright(text);
    };

    if (!walk(doc, on_start, on_end, on_text))
        return std::nullopt;
    return map;
}

std::optional<CharSet> Zx81Sidecars::char_set(std::string_view doc) const {
    TASTY_SEAT_BODY(Zx81Sidecars);
    CharSet set = CharSet::stock();
    std::optional<GlyphSlot> entry;
    std::optional<PixelLine> line;

    const auto on_start = [&](const xml::Tag& tag) {
        if (ieq(tag.name, "definition")) {
            entry.reset();
            line.reset();
        }
        if (ieq(tag.name, "entry"))
            entry = GlyphSlot::of_text(attr_text(tag.attrs, "code"));
        if (ieq(tag.name, "line"))
            line = PixelLine::of_text(attr_text(tag.attrs, "index"));
    };
    const auto on_end = [&](std::string_view name) {
        if (ieq(name, "line"))
            line.reset();
        if (ieq(name, "entry"))
            entry.reset();
    };

    const auto on_text = [&](std::string_view text) {
        if (!entry || !line)
            return;
        GlyphRow row{};
        if (const auto read = GlyphRow::of_text(text))
            row = entry->inverse() ? read->inverted() : *read;
        set.set(*entry, *line, row);
        line.reset();
    };

    if (!walk(doc, on_start, on_end, on_text))
        return std::nullopt;
    return set;
}

std::optional<std::vector<std::uint8_t>> Zx81Sidecars::read_sibling_(
    const svc::Vfs& vfs,
    std::string_view pick,
    SidecarClass which
) {
    std::string path(pick);
    path.resize(path.size() - infra::suffix_from_last_dot(pick).size());
    path.append(sibling_ext(which));
    auto f = vfs.open(path, svc::OpenMode::ReadWhole);
    if (!f)
        return std::nullopt;
    const auto size = (*f)->size();
    if (!size || *size > kMaxDocBytes)
        return std::nullopt;
    auto bytes = size->zeroed<std::vector<std::uint8_t>>();
    if (!(*f)->read_exact_at(proto::kFileStart, std::as_writable_bytes(std::span(bytes))))
        return std::nullopt;
    return bytes;
}

SidecarPlan Zx81Sidecars::plan(const SidecarAsk& ask, const svc::Vfs& vfs) {
    TASTY_SEAT_BODY(Zx81Sidecars);
    SidecarPlan out;
    const proto::FioIndexByte tape = ask.index.fio_byte();

    if (tape == proto::FioIndexByte{} || svc::asset_name::zip_split(ask.pick).zipped)
        return out;
    if (const auto doc = read_sibling_(vfs, ask.pick, SidecarClass::Colour)) {
        if (const auto map = colour_map(infra::as_text(std::span(*doc))))
            out.items.push_back(
                SidecarPayload{
                    .dest = sidecar_index(tape, SidecarClass::Colour),
                    .bytes = map->image()
                }
            );
    }
    if (const auto doc = read_sibling_(vfs, ask.pick, SidecarClass::Chars)) {
        if (const auto set = char_set(infra::as_text(std::span(*doc))))
            out.items.push_back(
                SidecarPayload{
                    .dest = sidecar_index(tape, SidecarClass::Chars),
                    .bytes = set->image()
                }
            );
    }
    return out;
}

}
