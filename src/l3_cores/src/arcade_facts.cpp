// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/arcade_facts.h"

#include <span>
#include <utility>

#include "svc/xml_scan.h"

namespace mister::cores {

namespace {

std::uint64_t saved_dips(const svc::Vfs& vfs, std::string_view name, std::uint64_t def) {
    auto f = vfs.open(std::string("config/dips/").append(name), svc::OpenMode::Read);
    if (!f)
        return def;
    std::array<std::uint8_t, 8> raw{};
    auto r = (*f)->read_at(0, std::as_writable_bytes(std::span<std::uint8_t>(raw)));
    if (!r || *r != raw.size())
        return def;
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        v |= static_cast<std::uint64_t>(raw[i]) << (8 * i);
    }
    return v;
}

struct Read {
    std::string text;
    std::string error;
};

Read read_text(const svc::Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, svc::OpenMode::Read);
    if (!f)
        return {.text = {}, .error = std::string(path) + "\nmanifest not found"};
    const auto sz = (*f)->size();
    if (!sz || sz->v == 0 || sz->v > mra::kMraDocMax) {
        return {.text = {}, .error = std::string(path) + "\nmanifest unreadable"};
    }
    std::string text(static_cast<std::size_t>(sz->v), '\0');
    std::size_t got = 0;
    while (got < text.size()) {
        auto r = (*f)->read_at(got, std::as_writable_bytes(std::span<char>(text).subspan(got)));
        if (!r || *r == 0)
            break;
        got += *r;
    }
    text.resize(got);
    return {.text = std::move(text), .error = {}};
}

}

ArcadeDocument read_arcade_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
) {
    ArcadeDocument out;
    if (path.empty())
        return out;
    ArcadeFacts& f = out.facts;
    f.dip_name = mra::sibling_name(path, ".dip");
    f.nvm_name = mra::sibling_name(path, ".nvm");
    std::string owned;
    if (text.empty()) {
        Read r = read_text(vfs, path);
        if (!r.error.empty()) {
            f.error = std::move(r.error);
            return out;
        }
        owned = std::move(r.text);
        text = owned;
    } else if (text.size() > mra::kMraDocMax) {
        f.error = std::string(path) + "\nmanifest unreadable";
        return out;
    }
    out.rbf = svc::xml::rbf_text(text);
    mra::MraDoc doc;
    doc.parse(text);
    out.roms = std::move(doc.roms);
    out.nvrams = std::move(doc.nvrams);
    f.nvram_index = doc.nvram_index;
    f.nvram_size = doc.nvram_size;
    f.btn_names = std::move(doc.buttons_names);
    f.btn_defaults = std::move(doc.buttons_default);
    f.switches = std::move(doc.switches);
    f.cheats = mra::MraCheatRecords{std::move(doc.cheats), doc.cheat_geometry};
    f.cheats_dropped = doc.cheats_dropped;
    f.dip_cur = saved_dips(vfs, f.dip_name, f.switches.dip_def);
    return out;
}

bool nvram_declared(int index, int size) noexcept {
    return index > 0 && index <= 255 && size > 0 && static_cast<std::uint64_t>(size) <= kNvramCap;
}

std::array<std::uint8_t, 8> dip_pack(std::uint64_t dip) noexcept {
    std::array<std::uint8_t, 8> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(dip >> (8 * i));
    }
    return out;
}

}
