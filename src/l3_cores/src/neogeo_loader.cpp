// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo_loader.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include "cores/neo_header.h"
#include "cores/neo_quirk.h"
#include "cores/neo_transform.h"
#include "cores/neogeo_file_index.h"
#include "cores/romset_file.h"
#include "cores/romset_plan.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/path_parts.h"
#include "svc/read_bounded.h"
#include "svc/search_policy.h"

namespace mister::cores {

namespace neogeo {

std::string make_path(std::string_view root, std::string_view name) {
    std::string path(root);
    while (name.starts_with("../")) {
        name.remove_prefix(3);
        path.resize(infra::dir_or_self(path).size());
    }
    path += '/';
    path.append(name);
    return path;
}

std::string save_path(std::string_view core, std::string_view pick) {
    std::string out = "saves/";
    out.append(core);
    out += '/';
    out += infra::stem(pick);
    out += ".sav";
    return out;
}

}

namespace {

[[nodiscard]] bool is_prom_name(std::string_view path) noexcept {
    return infra::ieq(infra::basename(path), "prom");
}

[[nodiscard]] neogeo::Shape entry_shape(const neogeo::RomsetFile& e) noexcept {
    using neogeo::FileClass;
    switch (e.index.class_of()) {
        case FileClass::Fix:
            return neogeo::Shape::Fix;
        case FileClass::Spr:
            return e.swap ? neogeo::Shape::SprDblSwap : neogeo::Shape::SprDbl;
        case FileClass::Raw:
            break;
    }
    return neogeo::Shape::None;
}

constexpr std::uint64_t kMaxXmlBytes = 16u * 1024u * 1024u;

}

namespace neogeo {

using SaveBuf = FixedStr<NeoGeoLoader::kPathChars, StrFit::Reject>;
using PathBufs = std::array<SaveBuf, kMaxTransferRows>;
using RowIx = LoadPlan::RowIx;

class PlanWalk final {
public:
    PlanWalk(
        const svc::Vfs& vfs,
        const CoreProfile& profile,
        PathBufs& paths,
        SaveBuf& save
    ) noexcept
        : vfs_(&vfs),
          profile_(&profile),
          paths_(&paths),
          save_(&save) {}

    [[nodiscard]] Ex<void> cart(std::string_view pick, const proto::StatusWord& status);

    [[nodiscard]] Ex<void> cd(std::string_view pick, const proto::StatusWord& status);

    [[nodiscard]] Ex<std::uint32_t> add_entry(const RomsetFile& e);

    [[nodiscard]] Ex<void> flush_crom();

    struct Result {
        LoadPlan plan;
        WalkTally tally;
    };

    [[nodiscard]] Result take() noexcept { return std::move(result_); }

private:
    struct Span {
        std::uint32_t size = 0;
        std::uint64_t rest = 0;
    };

    [[nodiscard]] std::optional<Span> entry_span(
        std::string_view path,
        std::uint32_t offset,
        std::uint32_t size
    ) const;
    [[nodiscard]] bool exists(std::string_view path) const;
    [[nodiscard]] std::string home_dir(std::string_view dir) const;
    [[nodiscard]] Ex<void> frame_save(std::string_view pick);

    [[nodiscard]] Ex<RowIx> add_row(std::string_view path, RowDest dest);
    [[nodiscard]] Ex<std::uint32_t> add_crom(const RomsetFile& e);
    [[nodiscard]] Ex<std::uint32_t> add_rom(
        std::string_view path,
        const RomsetFile& e,
        std::uint32_t concat
    );
    [[nodiscard]] Ex<void> add_notify(const PostNotify& n);
    [[nodiscard]] Ex<bool> add_lo_rom(std::string_view home);
    [[nodiscard]] Ex<void> add_fill();
    [[nodiscard]] Ex<std::optional<std::uint32_t>> add_neo(std::string_view pick);
    [[nodiscard]] Ex<std::optional<std::uint32_t>> add_xml(
        std::string_view pick,
        std::string_view home
    );

    Result result_{};
    const svc::Vfs* vfs_;
    const CoreProfile* profile_;
    PathBufs* paths_;
    SaveBuf* save_;
    std::uint32_t crom_sz_ = 0;
    std::optional<RowIx> last_crom_;
};

Ex<void> PlanWalk::cart(std::string_view pick, const proto::StatusWord& status) {
    const auto romset = infra::leaf_in_dir(pick);
    if (!romset)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    result_.plan.frame.hold_reset = true;
    const std::string home = home_dir({});
    if (auto r = add_notify(neogeo::make_conf_notify(0)); !r)
        return std::unexpected(r.error());

    std::optional<std::uint32_t> applied;
    if (infra::iends_with(pick, ".neo")) {
        auto r = add_neo(pick);
        if (!r)
            return std::unexpected(r.error());
        applied = *r;
    } else {
        auto r = add_xml(pick, home);
        if (!r)
            return std::unexpected(r.error());
        if (result_.plan.frame.refused)
            return {};
        applied = *r;
    }

    if (*romset != "debug") {
        const std::string uni = neogeo::make_path(home, "uni-bios.rom");
        std::string_view bios = status.get_bit(neogeo::kMvsBit) ? "sp-s2.sp1" : "neo-epo.sp1";
        if (!status.get_bit(neogeo::kOriginalBiosBit) && exists(uni))
            bios = "uni-bios.rom";
        const std::string path = neogeo::make_path(home, bios);
        const neogeo::RomsetFile
            e{.path = path, .index = neogeo::FileIndex::Bios, .offset = 0, .size = 0x20000};
        if (auto r = add_entry(e); !r)
            return std::unexpected(r.error());
    }
    if (auto r = flush_crom(); !r)
        return std::unexpected(r.error());
    const std::string sfix = neogeo::make_path(home, "sfix.sfix");
    const neogeo::FileIndex sfix_index = neogeo::FileIndex::Sfix;
    const neogeo::RomsetFile fix{.path = sfix, .index = sfix_index};
    if (auto r = add_entry(fix); !r)
        return std::unexpected(r.error());
    if (auto r = add_lo_rom(home); !r)
        return std::unexpected(r.error());

    result_.tally.config = applied.value_or(0u);
    if (auto r = add_notify(neogeo::make_conf_notify(result_.tally.config)); !r)
        return std::unexpected(r.error());
    if (auto r = frame_save(pick); !r)
        return std::unexpected(r.error());
    return {};
}

Ex<void> PlanWalk::cd(std::string_view pick, const proto::StatusWord& status) {
    const auto romset = infra::leaf_in_dir(pick);
    if (!romset)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 3});
    result_.plan.frame.hold_reset = true;
    const std::string home = home_dir(profile_->cue_browse_dir);
    if (auto r = add_notify(neogeo::make_conf_notify(neogeo::kCdEnBit)); !r)
        return std::unexpected(r.error());
    if (*romset != "debug") {
        if (auto r = add_fill(); !r)
            return std::unexpected(r.error());
        const std::string uni = neogeo::make_path(home, "uni-bioscd.rom");
        std::string_view bios = status.get_bit(neogeo::kCdzBit) ? "neocd.bin" : "top-sp1.bin";
        if (!status.get_bit(neogeo::kOriginalBiosBit) && exists(uni))
            bios = "uni-bioscd.rom";
        const std::string path = neogeo::make_path(home, bios);
        const neogeo::RomsetFile e{
            .path = path,
            .index = neogeo::FileIndex::Bios,
            .offset = 0,
            .size = neogeo::kCdBiosBytes
        };
        if (auto r = add_entry(e); !r)
            return std::unexpected(r.error());
    }
    if (auto r = flush_crom(); !r)
        return std::unexpected(r.error());
    auto lo = add_lo_rom(home);
    if (!lo)
        return std::unexpected(lo.error());
    if (!*lo) {
        if (auto r = add_lo_rom(home_dir({})); !r)
            return std::unexpected(r.error());
    }
    result_.tally.config = neogeo::kCdEnBit;
    if (auto r = add_notify(neogeo::make_conf_notify(result_.tally.config)); !r)
        return std::unexpected(r.error());
    if (auto r = frame_save(pick); !r)
        return std::unexpected(r.error());
    result_.plan.frame.release_reset = false;
    return {};
}

Ex<void> PlanWalk::frame_save(std::string_view pick) {
    const std::string_view core = profile_->name;
    const std::string save = neogeo::save_path(core, pick);
    std::string dir = "saves/";
    dir.append(core);
    (void)vfs_->ensure_dir(dir);
    if (!save_->assign(save))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    result_.plan.frame.save = save_->view();
    result_.plan.frame.release_reset = true;
    return {};
}

Ex<std::optional<std::uint32_t>> PlanWalk::add_neo(std::string_view pick) {
    std::optional<neogeo::NeoHeader> hdr;
    if (auto f = vfs_->open(pick, svc::OpenMode::Read)) {
        auto read = svc::read_bounded(**f, neogeo::kNeoHeaderBytes);
        if (read) {
            hdr = neogeo::parse_neo_header(infra::byte_cast<const std::uint8_t>(std::span(*read)));
        }
    }
    if (!hdr)
        return std::nullopt;
    const neogeo::CartQuirks q = neogeo::resolve_quirks(*hdr);
    const std::uint32_t expand = q.vrom_mirror ? 0u : neogeo::kVromSize;
    std::uint32_t off = static_cast<std::uint32_t>(neogeo::kNeoHeaderBytes);
    const auto tx = [&](std::uint32_t size, neogeo::RomsetFile e) -> Ex<void> {
        e.path = pick;
        e.offset = off;
        e.size = size;
        off += size;
        if (size == 0)
            return {};
        auto r = add_entry(e);
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
    neogeo::RomsetConfig cfg = q.config;
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
    return neogeo::config_word(cfg, false);
}

Ex<std::optional<std::uint32_t>> PlanWalk::add_xml(std::string_view pick, std::string_view home) {
    const std::string root(pick);
    const bool dspack = exists(neogeo::make_path(root, "crom0"))
        && exists(neogeo::make_path(root, "prom"));
    std::string xml = neogeo::make_path(root, "romset.xml");
    if (!exists(xml)) {
        xml = neogeo::make_path(infra::dir_or_self(root), "romsets.xml");
        if (!exists(xml))
            xml = neogeo::make_path(home, "romsets.xml");
    }
    std::string doc;
    if (auto f = vfs_->open(xml, svc::OpenMode::ReadWhole)) {
        const auto sz = (*f)->size();
        if (sz && sz->within(svc::FileSize{1u}, svc::FileSize{kMaxXmlBytes})) {
            doc.resize(static_cast<std::size_t>(sz->v));

            const auto got = svc::read_bounded_partial(
                **f,
                infra::byte_cast<std::byte>(std::span<char>(doc))
            );
            doc.resize(got.size());
        }
    }
    neogeo::RomsetPlan plan = neogeo::load_files(doc, neogeo::get_romset(pick));
    if (!dspack) {
        bool ok = plan.found;
        for (const neogeo::RomsetFile& f : plan.files) {
            if (!ok)
                break;
            if (!f.name.empty() && !exists(neogeo::make_path(root, f.name)))
                ok = false;
        }
        if (!ok) {
            result_.plan.frame.refused = true;
            return std::nullopt;
        }
    }
    if (!plan.found)
        return std::nullopt;

    const auto load = [&](neogeo::RomsetFile& f) -> Ex<std::uint32_t> {
        f.path = neogeo::make_path(root, f.name);
        return add_entry(f);
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

std::optional<PlanWalk::Span> PlanWalk::entry_span(
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

bool PlanWalk::exists(std::string_view path) const {
    if (vfs_->dir_exists(path))
        return false;
    return vfs_->open(path, svc::OpenMode::Read).has_value();
}

std::string PlanWalk::home_dir(std::string_view dir) const {
    const std::string_view name = !dir.empty() ? dir : profile_->name;
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

Ex<RowIx> PlanWalk::add_row(std::string_view path, RowDest dest) {
    if (result_.plan.count >= result_.plan.rows.size())
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(result_.plan.count)}
        );
    if (!(*paths_)[result_.plan.count].assign(path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const RowIx ix{result_.plan.count};
    TransferRow& r = result_.plan[ix];
    r = TransferRow{};
    r.dest = dest;
    r.window = WindowRef::at(neogeo::kMemWindowSlot);
    r.bracket = RowBracket::None;
    r.source = (*paths_)[result_.plan.count].view();
    ++result_.plan.count;
    return ix;
}

Ex<void> PlanWalk::add_notify(const PostNotify& n) {
    auto i = add_row({}, RowDest::Notify);
    if (!i)
        return std::unexpected(i.error());
    TransferRow& row = result_.plan[*i];
    row.index = proto::WideIoIndex{PostNotify::kIndex};
    row.notify = n;
    return {};
}

Ex<std::uint32_t> PlanWalk::add_entry(const neogeo::RomsetFile& e) {
    if (e.index.is_crom())
        return add_crom(e);
    if (auto r = flush_crom(); !r)
        return std::unexpected(r.error());
    auto first = add_rom(e.path, e, 0);
    if (!first)
        return std::unexpected(first.error());
    if (*first == 0) {
        ++result_.tally.skipped;
        return 0u;
    }
    std::uint32_t sz = *first;
    if (e.index == neogeo::FileIndex::Prom && is_prom_name(e.path)) {
        const std::string sibling = std::string(infra::dir_prefix(e.path)) + "prom1";
        auto second = add_rom(sibling, e, sz);
        if (!second)
            return std::unexpected(second.error());
        sz += *second;
    }
    result_.plan[RowIx{result_.plan.count - 1}].notify = neogeo::make_notify(e.index, sz);
    return sz;
}

Ex<void> PlanWalk::flush_crom() {
    if (crom_sz_ == 0)
        return {};
    const PostNotify n = neogeo::make_notify(neogeo::FileIndex::Sprite, crom_sz_);
    crom_sz_ = 0;
    if (last_crom_) {
        result_.plan[*last_crom_].notify = n;
        last_crom_.reset();
        return {};
    }
    return add_notify(n);
}

Ex<std::uint32_t> PlanWalk::add_crom(const neogeo::RomsetFile& e) {
    const auto s = entry_span(e.path, e.offset, e.size);
    if (!s || s->size == 0) {
        ++result_.tally.skipped;
        return 0u;
    }
    const std::uint32_t end = e.index.crom_offset() + 2u * s->size;
    crom_sz_ = std::max(crom_sz_, end);
    const auto readable = static_cast<std::uint32_t>(std::min<std::uint64_t>(s->size, s->rest));
    if (readable == 0) {
        ++result_.tally.skipped;
        return end;
    }
    auto i = add_row(e.path, RowDest::Window);
    if (!i)
        return std::unexpected(i.error());
    TransferRow& row = result_.plan[*i];
    row.shaped = true;
    row.shape = neogeo::shape_key_of(neogeo::Shape::Crom);
    row.index = e.index.wide();
    row.addr = e.index.crom_offset();
    row.stride = 2;
    row.lane = e.index.crom_lane();
    row.file_offset = e.offset;
    row.file_len = proto::FileSize{readable};
    row.extent = proto::FileSize{2u * readable};
    last_crom_ = *i;
    return end;
}

Ex<std::uint32_t> PlanWalk::add_rom(
    std::string_view path,
    const neogeo::RomsetFile& e,
    std::uint32_t concat
) {
    const auto s = entry_span(path, e.offset, e.size);
    if (!s || s->size == 0)
        return 0u;
    const std::uint32_t extent = e.expand != 0 ? e.expand : s->size;
    auto i = add_row(path, RowDest::Window);
    if (!i)
        return std::unexpected(i.error());
    TransferRow& row = result_.plan[*i];
    const neogeo::Shape shape = entry_shape(e);
    const neogeo::RomPlace place = neogeo::rom_place(e.index, concat);
    row.shaped = shape != neogeo::Shape::None;
    row.shape = neogeo::shape_key_of(shape);
    row.index = e.index.wide();
    row.window = place.window;
    row.addr = place.offset;
    row.fill = row.shaped ? 0u : e.index.vrom_fill();
    row.file_offset = e.offset;
    row.file_len = proto::FileSize{neogeo::stock_read_len(s->size, extent, s->rest)};
    row.extent = proto::FileSize{extent};
    return extent;
}

Ex<void> PlanWalk::add_fill() {
    auto i = add_row({}, RowDest::Window);
    if (!i)
        return std::unexpected(i.error());
    TransferRow& row = result_.plan[*i];
    const neogeo::FileIndex fill_index = neogeo::FileIndex::Fill;
    row.index = fill_index.wide();
    row.fill = neogeo::kFillPattern;
    row.extent = proto::FileSize{neogeo::kFillBytes};
    row.notify = neogeo::make_notify(fill_index, neogeo::kFillBytes, true);
    return {};
}

Ex<bool> PlanWalk::add_lo_rom(std::string_view home) {
    const std::string path = neogeo::make_path(home, "000-lo.lo");
    const auto s = entry_span(path, 0, neogeo::kLoRomBytes);
    if (!s)
        return false;
    auto i = add_row(path, RowDest::Payload);
    if (!i)
        return std::unexpected(i.error());
    TransferRow& row = result_.plan[*i];
    const neogeo::FileIndex lo_index = neogeo::FileIndex::LoRom;
    row.index = lo_index.wide();
    row.byte_words = true;
    row.file_len = proto::FileSize{std::min<std::uint64_t>(s->rest, neogeo::kLoRomBytes)};
    row.extent = proto::FileSize{neogeo::kLoRomBytes};
    return true;
}

}

Ex<LoadPlan> NeoGeoLoader::plan(const LoadAsk& ask, const proto::StatusWord& status) {
    TASTY_SEAT_BODY(NeoGeoLoader);
    if (!ask.disc && !walks_romset(profile_.slots, ask.index.io())) {
        LoadPlan p{};
        p.count = 1;
        p.rows[0].index = ask.index;
        return p;
    }
    neogeo::PlanWalk walk(*vfs_, profile_, paths_, save_);
    const Ex<void> r = ask.disc ? walk.cd(ask.path, status) : walk.cart(ask.path, status);
    neogeo::PlanWalk::Result took = walk.take();
    walked_ = took.tally;
    if (!r)
        return std::unexpected(r.error());
    return std::move(took.plan);
}

Ex<LoadPlan> NeoGeoLoader::plan_romset(std::span<const neogeo::RomsetFile> entries) {
    neogeo::PlanWalk walk(*vfs_, profile_, paths_, save_);
    const Ex<void> walked = [&]() -> Ex<void> {
        for (const neogeo::RomsetFile& e : entries) {
            if (auto r = walk.add_entry(e); !r)
                return std::unexpected(r.error());
        }
        return walk.flush_crom();
    }();
    neogeo::PlanWalk::Result took = walk.take();
    walked_ = took.tally;
    if (!walked)
        return std::unexpected(walked.error());
    return std::move(took.plan);
}

neogeo::IRomShape* NeoGeoLoader::shaper_(neogeo::Shape kind) noexcept {
    switch (kind) {
        case neogeo::Shape::None:
            return nullptr;
        case neogeo::Shape::Crom:
            return &spr_shape_;
        case neogeo::Shape::Fix:
            return &fix_shape_;
        case neogeo::Shape::SprDbl:
            return &spr_dbl_shape_;
        case neogeo::Shape::SprDblSwap:
            return &spr_dbl_swap_shape_;
    }
    return nullptr;
}

void NeoGeoLoader::shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t) {
    TASTY_SEAT_BODY(NeoGeoLoader);
    const std::optional<neogeo::Shape> kind = neogeo::shape_of(row.shape);
    if (!kind)
        return;
    neogeo::IRomShape* const shaper = shaper_(*kind);
    if (shaper == nullptr)
        return;
    shaper->transform(piece);
}

}
