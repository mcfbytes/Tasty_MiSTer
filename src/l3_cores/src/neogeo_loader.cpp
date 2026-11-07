// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo_loader.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include "cores/neo_file.h"
#include "cores/neo_transform.h"
#include "infra/endian.h"
#include "svc/search_policy.h"

namespace mister::cores {

namespace neogeo {

std::string make_path(std::string_view root, std::string_view name) {
    std::string path(root);
    while (name.starts_with("../")) {
        name.remove_prefix(3);
        const std::size_t p = path.rfind('/');
        if (p != std::string::npos)
            path.resize(p);
    }
    path += '/';
    path.append(name);
    return path;
}

std::string save_path(std::string_view core, std::string_view pick) {
    std::string out = "saves/";
    out.append(core);
    out += '/';
    const std::size_t slash = pick.rfind('/');
    std::string name(slash == std::string_view::npos ? pick : pick.substr(slash + 1));
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name.resize(dot);
    out += name;
    out += ".sav";
    return out;
}

}

namespace {

[[nodiscard]] char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

[[nodiscard]] std::string_view last_component(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] bool is_prom_name(std::string_view path) noexcept {
    return iequal(last_component(path), "prom");
}

[[nodiscard]] bool ends_with_ci(std::string_view s, std::string_view tail) noexcept {
    return s.size() >= tail.size() && iequal(s.substr(s.size() - tail.size()), tail);
}

[[nodiscard]] neogeo::Shape shape_of(const neogeo::RomsetFile& e) noexcept {
    using neogeo::FileClass;
    switch (e.file_class) {
        case FileClass::Fix:
            return neogeo::Shape::Fix;
        case FileClass::Spr:
            return e.swap ? neogeo::Shape::SprDblSwap : neogeo::Shape::SprDbl;
        case FileClass::Raw:
        case FileClass::EightBit:
            break;
    }
    return neogeo::Shape::None;
}

constexpr std::uint64_t kMaxXmlBytes = 16u * 1024u * 1024u;

}

Ex<LoadPlan> NeoGeoLoader::plan(const LoadAsk& ask, const proto::StatusWord& status) {
    TASTY_SEAT_BODY(NeoGeoLoader);
    if (ask.disc)
        return plan_cd_(ask.path, status);
    if (profile_ != nullptr && walks_romset(profile_->slots, ask.index.io()))
        return plan_cart_(ask.path, status);
    LoadPlan p{};
    p.count = 1;
    p.rows[0].index = ask.index;
    return p;
}

void NeoGeoLoader::reset_walk_() noexcept {
    skipped_ = 0;
    crom_sz_ = 0;
    last_crom_ = kMaxTransferRows;
    config_ = 0;
}

Ex<LoadPlan> NeoGeoLoader::plan_romset(std::span<const neogeo::RomsetFile> entries) {
    LoadPlan p{};
    reset_walk_();
    for (const neogeo::RomsetFile& e : entries) {
        if (auto r = add_entry_(p, e); !r)
            return std::unexpected(r.error());
    }
    if (auto r = flush_crom_(p); !r)
        return std::unexpected(r.error());
    return p;
}

Ex<LoadPlan> NeoGeoLoader::plan_cart_(std::string_view pick, const proto::StatusWord& status) {
    const std::size_t slash = pick.rfind('/');
    if (slash == std::string_view::npos)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const std::string_view romset = pick.substr(slash + 1);
    LoadPlan p{};
    reset_walk_();
    p.frame.hold_reset = true;
    const std::string home = home_({});
    if (auto r = add_notify_(p, neogeo::make_conf_notify(0)); !r)
        return std::unexpected(r.error());

    std::optional<std::uint32_t> applied;
    if (ends_with_ci(pick, ".neo")) {
        auto r = add_neo_(p, pick);
        if (!r)
            return std::unexpected(r.error());
        applied = *r;
    } else {
        auto r = add_xml_(p, pick, home);
        if (!r)
            return std::unexpected(r.error());
        if (p.frame.refused)
            return p;
        applied = *r;
    }

    if (romset != "debug") {
        const std::string uni = neogeo::make_path(home, "uni-bios.rom");
        std::string_view bios = status.get_bit(neogeo::kMvsBit) ? "sp-s2.sp1" : "neo-epo.sp1";
        if (!status.get_bit(neogeo::kOriginalBiosBit) && exists_(uni))
            bios = "uni-bios.rom";
        const std::string path = neogeo::make_path(home, bios);
        const neogeo::RomsetFile
            e{.path = path, .index = neogeo::FileIndex::Bios, .offset = 0, .size = 0x20000};
        if (auto r = add_entry_(p, e); !r)
            return std::unexpected(r.error());
    }
    if (auto r = flush_crom_(p); !r)
        return std::unexpected(r.error());
    const std::string sfix = neogeo::make_path(home, "sfix.sfix");
    const neogeo::FileIndex sfix_index{neogeo::FileIndex::Sfix};
    const neogeo::RomsetFile fix{
        .path = sfix,
        .index = sfix_index,
        .file_class = sfix_index.class_of()
    };
    if (auto r = add_entry_(p, fix); !r)
        return std::unexpected(r.error());
    if (auto r = add_lo_rom_(p, home); !r)
        return std::unexpected(r.error());

    config_ = applied.value_or(0u);
    if (auto r = add_notify_(p, neogeo::make_conf_notify(config_)); !r)
        return std::unexpected(r.error());
    if (auto r = frame_save_(p, pick); !r)
        return std::unexpected(r.error());
    return p;
}

Ex<LoadPlan> NeoGeoLoader::plan_cd_(std::string_view pick, const proto::StatusWord& status) {
    const std::size_t slash = pick.rfind('/');
    if (slash == std::string_view::npos)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 3});
    const std::string_view romset = pick.substr(slash + 1);
    LoadPlan p{};
    reset_walk_();
    p.frame.hold_reset = true;
    const std::string home = home_(
        profile_ != nullptr ? profile_->cue_browse_dir : std::string_view{}
    );
    if (auto r = add_notify_(p, neogeo::make_conf_notify(neogeo::kCdEnBit)); !r)
        return std::unexpected(r.error());
    if (romset != "debug") {
        if (auto r = add_fill_(p); !r)
            return std::unexpected(r.error());
        const std::string uni = neogeo::make_path(home, "uni-bioscd.rom");
        std::string_view bios = status.get_bit(neogeo::kCdzBit) ? "neocd.bin" : "top-sp1.bin";
        if (!status.get_bit(neogeo::kOriginalBiosBit) && exists_(uni))
            bios = "uni-bioscd.rom";
        const std::string path = neogeo::make_path(home, bios);
        const neogeo::RomsetFile e{
            .path = path,
            .index = neogeo::FileIndex::Bios,
            .offset = 0,
            .size = neogeo::kCdBiosBytes
        };
        if (auto r = add_entry_(p, e); !r)
            return std::unexpected(r.error());
    }
    if (auto r = flush_crom_(p); !r)
        return std::unexpected(r.error());
    auto lo = add_lo_rom_(p, home);
    if (!lo)
        return std::unexpected(lo.error());
    if (!*lo) {
        if (auto r = add_lo_rom_(p, home_({})); !r)
            return std::unexpected(r.error());
    }
    config_ = neogeo::kCdEnBit;
    if (auto r = add_notify_(p, neogeo::make_conf_notify(config_)); !r)
        return std::unexpected(r.error());
    if (auto r = frame_save_(p, pick); !r)
        return std::unexpected(r.error());
    p.frame.release_reset = false;
    return p;
}

Ex<void> NeoGeoLoader::frame_save_(LoadPlan& p, std::string_view pick) {
    const std::string_view core = profile_ != nullptr ? profile_->name : std::string_view{"NEOGEO"};
    const std::string save = neogeo::save_path(core, pick);
    std::string dir = "saves/";
    dir.append(core);
    (void)vfs_->ensure_dir(dir);
    if (!save_.assign(save))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    p.frame.save = save_.view();
    p.frame.release_reset = true;
    return {};
}

Ex<std::optional<std::uint32_t>> NeoGeoLoader::add_neo_(LoadPlan& p, std::string_view pick) {
    std::optional<neogeo::NeoHeader> hdr;
    if (auto f = vfs_->open(pick, svc::OpenMode::Read)) {
        bytes_.assign(neogeo::kNeoHeaderBytes, 0);
        std::size_t got = 0;
        while (got < bytes_.size()) {
            auto n = (*f)->read_at(got, std::as_writable_bytes(std::span(bytes_).subspan(got)));
            if (!n || *n == 0)
                break;
            got += *n;
        }
        hdr = neogeo::parse_neo_header(std::span(bytes_).first(got));
    }
    if (!hdr)
        return std::nullopt;
    const neogeo::NeoCartConfig q = neogeo::resolve_quirks(*hdr);
    const std::uint32_t expand = q.vrom_mirror ? 0u : neogeo::kVromSize;
    std::uint32_t off = static_cast<std::uint32_t>(neogeo::kNeoHeaderBytes);
    const auto tx = [&](std::uint32_t size, neogeo::RomsetFile e) -> Ex<void> {
        e.path = pick;
        e.offset = off;
        e.size = size;
        e.file_class = e.index.class_of();
        off += size;
        if (size == 0)
            return {};
        auto r = add_entry_(p, e);
        if (!r)
            return std::unexpected(r.error());
        return {};
    };
    if (auto r = tx(hdr->p_size, {.index = neogeo::FileIndex::Prom}); !r)
        return std::unexpected(r.error());
    if (auto r = tx(hdr->s_size, {.index = neogeo::FileIndex::Srom}); !r)
        return std::unexpected(r.error());
    if (auto r = tx(hdr->m_size, {.index = neogeo::FileIndex::M1}); !r)
        return std::unexpected(r.error());
    if (auto r = tx(hdr->v1_size, {.index = neogeo::FileIndex::VromFirst, .expand = expand}); !r)
        return std::unexpected(r.error());
    neogeo::RomsetConfig cfg{};
    cfg.use_pcm = 1;
    if (hdr->v2_size != 0) {
        cfg.use_pcm = 0;
        if (auto r = tx(hdr->v2_size, {.index = neogeo::FileIndex::VromB, .expand = expand}); !r)
            return std::unexpected(r.error());
    } else if (hdr->v1_size > neogeo::kVromSize) {
        cfg.use_pcm = 2;
    }
    if (auto r = tx(hdr->c_size, {.index = neogeo::FileIndex::Sprite, .swap = true}); !r)
        return std::unexpected(r.error());
    cfg.hw_type = q.hw_type;
    cfg.cmc = q.cmc;
    cfg.pvc = q.pvc;
    cfg.sma = q.sma;
    cfg.xram = q.xram;
    cfg.rom_wait = q.rom_wait;
    cfg.p_wait = q.p_wait;
    cfg.ms5p = q.ms5p;
    return neogeo::config_word(cfg, false);
}

Ex<std::optional<std::uint32_t>> NeoGeoLoader::add_xml_(
    LoadPlan& p,
    std::string_view pick,
    std::string_view home
) {
    const std::string root(pick);
    const bool dspack = exists_(neogeo::make_path(root, "crom0"))
        && exists_(neogeo::make_path(root, "prom"));
    std::string xml = neogeo::make_path(root, "romset.xml");
    if (!exists_(xml)) {
        const std::size_t slash = root.rfind('/');
        xml = neogeo::make_path(root.substr(0, slash), "romsets.xml");
        if (!exists_(xml))
            xml = neogeo::make_path(home, "romsets.xml");
    }
    std::string doc;
    if (auto f = vfs_->open(xml, svc::OpenMode::ReadWhole)) {
        const auto sz = (*f)->size();
        if (sz && sz->v <= kMaxXmlBytes) {
            doc.resize(static_cast<std::size_t>(sz->v));
            std::size_t got = 0;
            while (got < doc.size()) {
                auto n = (*f)->read_at(got, std::as_writable_bytes(std::span(doc).subspan(got)));
                if (!n || *n == 0)
                    break;
                got += *n;
            }
            doc.resize(got);
        }
    }
    neogeo::RomsetPlan plan = neogeo::load_files(doc, neogeo::get_romset(pick));
    if (!dspack) {
        bool ok = plan.found;
        for (const neogeo::RomsetFile& f : plan.files) {
            if (!ok)
                break;
            if (!f.name.empty() && !exists_(neogeo::make_path(root, f.name)))
                ok = false;
        }
        if (!ok) {
            p.frame.refused = true;
            return std::nullopt;
        }
    }
    if (!plan.found)
        return std::nullopt;

    const auto load = [&](neogeo::RomsetFile& f) -> Ex<std::uint32_t> {
        f.path = neogeo::make_path(root, f.name);
        return add_entry_(p, f);
    };
    int loaded = 0;
    for (neogeo::RomsetFile& f : plan.files) {
        auto r = load(f);
        if (!r)
            return std::unexpected(r.error());
        if (*r != 0)
            ++loaded;
    }
    neogeo::RomsetConfig cfg = plan.config;
    if (loaded == 0) {

        if (!plan.no_files_declared && plan.vromb_offset != 0 && !plan.force_pcm)
            cfg.use_pcm = 0;
        std::vector<neogeo::RomsetFile> fallback = neogeo::fallback_files(plan);
        for (neogeo::RomsetFile& f : fallback) {
            auto r = load(f);
            if (!r)
                return std::unexpected(r.error());
            if (*r != 0 && f.name == "vromb0" && !plan.force_pcm)
                cfg.use_pcm = 0;
        }
    }
    return neogeo::config_word(cfg, false);
}

std::optional<NeoGeoLoader::Span> NeoGeoLoader::entry_span_(
    std::string_view path,
    std::uint32_t offset,
    std::uint32_t size
) const {
    auto f = vfs_->open(path, svc::OpenMode::Read);
    if (!f)
        return std::nullopt;
    const auto fs = (*f)->size();
    if (!fs)
        return std::nullopt;
    const std::uint64_t rest = fs->v > offset ? fs->v - offset : 0u;
    if (size == 0)
        size = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(rest, std::numeric_limits<std::uint32_t>::max())
        );
    return Span{.size = size, .rest = rest};
}

bool NeoGeoLoader::exists_(std::string_view path) const {
    if (vfs_->dir_exists(path))
        return false;
    return vfs_->open(path, svc::OpenMode::Read).has_value();
}

std::string NeoGeoLoader::home_(std::string_view dir) const {
    const std::string_view name = !dir.empty()
        ? dir
        : (profile_ != nullptr ? profile_->name : std::string_view{"NEOGEO"});
    static constexpr svc::SearchDir kHome[] = {svc::SearchDir::CoreDir};
    if (auto r = vfs_->resolve(name, svc::SearchPolicy{kHome, true})) {
        const std::string& root = vfs_->root_path();
        if (r->size() > root.size() + 1 && r->starts_with(root) && (*r)[root.size()] == '/')
            return r->substr(root.size() + 1);
        return *r;
    }
    std::string home = "games/";
    home.append(name);
    return home;
}

Ex<TransferRow*> NeoGeoLoader::add_row_(LoadPlan& p, std::string_view path, RowDest dest) {
    if (p.count >= p.rows.size())
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(p.count)}
        );
    if (!paths_[p.count].assign(path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    TransferRow& r = p.rows[p.count];
    r = TransferRow{};
    r.dest = dest;
    r.window = WindowRef::at(neogeo::kMemWindowSlot);
    r.bracket = RowBracket::None;
    r.source = paths_[p.count].view();
    ++p.count;
    return &r;
}

Ex<void> NeoGeoLoader::add_notify_(LoadPlan& p, const PostNotify& n) {
    auto r = add_row_(p, {}, RowDest::Notify);
    if (!r)
        return std::unexpected(r.error());
    (*r)->index = proto::WideIoIndex{PostNotify::kIndex};
    (*r)->notify = n;
    return {};
}

Ex<std::uint32_t> NeoGeoLoader::add_entry_(LoadPlan& p, const neogeo::RomsetFile& e) {
    if (e.index.is_crom())
        return add_crom_(p, e);
    if (auto r = flush_crom_(p); !r)
        return std::unexpected(r.error());
    auto first = add_rom_(p, e.path, e, 0);
    if (!first)
        return std::unexpected(first.error());
    if (*first == 0) {
        ++skipped_;
        return 0u;
    }
    std::uint32_t sz = *first;
    if (e.index == neogeo::FileIndex::Prom && is_prom_name(e.path)) {
        const std::size_t slash = e.path.rfind('/');
        const std::string sibling = slash == std::string_view::npos
            ? std::string("prom1")
            : std::string(e.path.substr(0, slash + 1)) + "prom1";
        auto second = add_rom_(p, sibling, e, sz);
        if (!second)
            return std::unexpected(second.error());
        sz += *second;
    }
    p.rows[p.count - 1].notify = neogeo::make_notify(e.index, sz);
    return sz;
}

Ex<void> NeoGeoLoader::flush_crom_(LoadPlan& p) {
    if (crom_sz_ == 0)
        return {};
    const PostNotify n = neogeo::make_notify(neogeo::FileIndex::Sprite, crom_sz_);
    crom_sz_ = 0;
    if (last_crom_ < p.count) {
        p.rows[last_crom_].notify = n;
        last_crom_ = kMaxTransferRows;
        return {};
    }
    return add_notify_(p, n);
}

Ex<std::uint32_t> NeoGeoLoader::add_crom_(LoadPlan& p, const neogeo::RomsetFile& e) {
    const auto s = entry_span_(e.path, e.offset, e.size);
    if (!s || s->size == 0) {
        ++skipped_;
        return 0u;
    }
    const std::uint32_t end = e.index.crom_offset() + 2u * s->size;
    crom_sz_ = std::max(crom_sz_, end);
    const auto readable = static_cast<std::uint32_t>(std::min<std::uint64_t>(s->size, s->rest));
    if (readable == 0) {
        ++skipped_;
        return end;
    }
    auto r = add_row_(p, e.path, RowDest::Window);
    if (!r)
        return std::unexpected(r.error());
    TransferRow& row = **r;
    row.shaped = true;
    row.shape = static_cast<std::uint8_t>(neogeo::Shape::Crom);
    row.index = e.index.wide();
    row.addr = e.index.crom_offset();
    row.stride = 2;
    row.lane = e.index.crom_lane();
    row.file_offset = e.offset;
    row.file_len = readable;
    row.extent = 2u * readable;
    last_crom_ = p.count - 1;
    return end;
}

Ex<std::uint32_t> NeoGeoLoader::add_rom_(
    LoadPlan& p,
    std::string_view path,
    const neogeo::RomsetFile& e,
    std::uint32_t concat
) {
    const auto s = entry_span_(path, e.offset, e.size);
    if (!s || s->size == 0)
        return 0u;
    const std::uint32_t extent = e.expand != 0 ? e.expand : s->size;
    auto r = add_row_(p, path, RowDest::Window);
    if (!r)
        return std::unexpected(r.error());
    TransferRow& row = **r;
    const neogeo::Shape shape = shape_of(e);
    const neogeo::RomPlace place = neogeo::rom_place(e.index, concat);
    row.shaped = shape != neogeo::Shape::None;
    row.shape = static_cast<std::uint8_t>(shape);
    row.index = e.index.wide();
    row.window = place.window;
    row.addr = place.offset;
    row.fill = row.shaped ? 0u : e.index.vrom_fill();
    row.file_offset = e.offset;
    row.file_len = neogeo::stock_read_len(s->size, extent, s->rest);
    row.extent = extent;
    return extent;
}

Ex<void> NeoGeoLoader::add_fill_(LoadPlan& p) {
    auto r = add_row_(p, {}, RowDest::Window);
    if (!r)
        return std::unexpected(r.error());
    TransferRow& row = **r;
    const neogeo::FileIndex fill_index{neogeo::FileIndex::Fill};
    row.index = fill_index.wide();
    row.fill = neogeo::kFillPattern;
    row.extent = neogeo::kFillBytes;
    row.notify = neogeo::make_notify(fill_index, neogeo::kFillBytes, true);
    return {};
}

Ex<bool> NeoGeoLoader::add_lo_rom_(LoadPlan& p, std::string_view home) {
    const std::string path = neogeo::make_path(home, "000-lo.lo");
    const auto s = entry_span_(path, 0, neogeo::kLoRomBytes);
    if (!s)
        return false;
    auto r = add_row_(p, path, RowDest::Payload);
    if (!r)
        return std::unexpected(r.error());
    TransferRow& row = **r;
    const neogeo::FileIndex lo_index{neogeo::FileIndex::LoRom};
    row.index = lo_index.wide();
    row.byte_words = true;
    row.file_len = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(s->rest, neogeo::kLoRomBytes)
    );
    row.extent = neogeo::kLoRomBytes;
    return true;
}

void NeoGeoLoader::shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t) {
    TASTY_SEAT_BODY(NeoGeoLoader);
    const auto kind = static_cast<neogeo::Shape>(row.shape);
    if (kind == neogeo::Shape::Fix) {
        const auto n = static_cast<std::uint32_t>(piece.size());

        bytes_.assign(neogeo::block32_extent(n), std::uint8_t{0});
        fix_out_.resize(n);
        std::memcpy(bytes_.data(), piece.data(), n);
        neogeo::fix_convert(bytes_, fix_out_, n);
        std::memcpy(piece.data(), fix_out_.data(), n);
        return;
    }
    const auto words = static_cast<std::uint32_t>(piece.size() / 2u);
    const bool dbl = kind == neogeo::Shape::SprDbl || kind == neogeo::Shape::SprDblSwap;
    if (!dbl && kind != neogeo::Shape::Crom)
        return;
    in_.assign(
        dbl ? neogeo::block64_extent(words) : neogeo::block32_extent(words),
        std::uint16_t{0}
    );
    out_.resize(words);
    infra::load_le_run<std::uint16_t>(piece, std::span(in_).first(words));
    if (kind == neogeo::Shape::SprDblSwap) {
        dwords_.resize(in_.size() / 2u);
        for (std::size_t i = 0; i < dwords_.size(); ++i) {
            dwords_[i] = std::uint32_t{in_[2 * i]} | (std::uint32_t{in_[2 * i + 1]} << 16);
        }
        neogeo::spr_bswap(dwords_, static_cast<std::uint32_t>(piece.size() / 4u));
        for (std::size_t i = 0; i < dwords_.size(); ++i) {
            in_[2 * i] = static_cast<std::uint16_t>(dwords_[i]);
            in_[2 * i + 1] = static_cast<std::uint16_t>(dwords_[i] >> 16);
        }
    }
    if (dbl) {
        neogeo::spr_convert_dbl(in_, out_, words);
    } else {
        neogeo::spr_convert(in_, out_, words);
    }
    infra::store_le_run<std::uint16_t>(std::span<const std::uint16_t>(out_).first(words), piece);
}

}
