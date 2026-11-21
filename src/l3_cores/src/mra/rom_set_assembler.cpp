// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra/rom_set_assembler.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <span>
#include <variant>

#include "cores/mra/assembled_rom.h"
#include "cores/mra/mra_doc.h"
#include "cores/mra/rom.h"
#include "cores/mra/rom_node.h"
#include "infra/ascii.h"
#include "infra/prelude.h"

namespace mister::cores::mra {
RomSetAssembler::RomSetAssembler(const svc::Vfs& vfs, std::string_view mame_root)
    : vfs_(vfs),
      mame_root_(mame_root) {}

void RomSetAssembler::rom_start() noexcept {
    data_.clear();
    romblkl_ = RomOffset{};
    for (RomOffset& lane_end : romlen_)
        lane_end = RomOffset{};
    unitlen_ = UnitLen{};
    inside_ilv_ = false;
    md5_ = Md5{};
    error_overwritten_ = false;
    runs_.clear();
    run_total_ = RomLength{};
    hw_ = RomOffset{};
    sized_out_ = false;
    plan_extent_ = RomOffset{};
    plan_ = RomPlan{};
    digest_.clear();
}

void RomSetAssembler::clear_error() noexcept {
    error_.clear();
    error_overwritten_ = true;
}

AssembledRom RomSetAssembler::assemble(const Rom& rom) {
    rom_start();

    if (rom.index == proto::WideIoIndex{} && !rom.zips.empty())
        clear_error();
    for (const RomNode& n : rom.nodes) {
        if (const auto* part = std::get_if<FilePart>(&n)) {
            part_node(rom, *part);
        } else if (const auto* text = std::get_if<TextPart>(&n)) {
            part_node(rom, *text);
        } else if (const auto* patch = std::get_if<Patch>(&n)) {
            patch_node(*patch);
        } else if (const auto* start = std::get_if<InterleaveStart>(&n)) {
            interleave_start(*start);
        } else if (std::get_if<InterleaveEnd>(&n) != nullptr) {
            interleave_end();
        }
    }
    return finish(rom);
}

AssembledRom RomSetAssembler::plan(const Rom& rom) {
    planning_ = true;
    AssembledRom out = assemble(rom);
    planning_ = false;
    return out;
}

bool RomSetAssembler::admit_part(const Rom& rom, const PartCommon& common) {

    if (!inside_ilv_ && !common.map.is_none()) {
        unitlen_ = UnitLen::of_map_digits(common.map.digits);
        for (const LaneIndex lane : kLanesAfterFirst) {
            romlen_[lane] = romlen_[LaneIndex{}];
        }
    }

    return !(rom.index == proto::WideIoIndex{} && validrom0_);
}

void RomSetAssembler::refuse_repeat() {
    ++repeat_refusals_;
    if (error_empty())
        error_ = "part repeat over cap";
}

void RomSetAssembler::part_node(const Rom& rom, const FilePart& p) {
    if (!admit_part(rom, p.common))
        return;
    if (p.common.repeat > kRepeatCap) {
        refuse_repeat();
    } else {
        file_part(rom, p);
    }
    if (!inside_ilv_)
        unitlen_ = UnitLen::one();
}

void RomSetAssembler::part_node(const Rom& rom, const TextPart& t) {
    if (!admit_part(rom, t.common))
        return;
    if (t.common.repeat > kRepeatCap) {
        if (fill_run(t)) {
            if (!inside_ilv_)
                unitlen_ = UnitLen::one();
            return;
        }
        refuse_repeat();
    } else {
        text_part(t);
    }
    if (!inside_ilv_)
        unitlen_ = UnitLen::one();
}

bool RomSetAssembler::file_part(const Rom& rom, const FilePart& p) {
    const std::string& zlist = p.zips.empty() ? rom.zips : p.zips;
    bool result = false;
    std::string fname;
    std::string_view rest = zlist;
    while (true) {
        const std::size_t bar = rest.find('|');
        const std::string_view zip = (bar == std::string_view::npos) ? rest : rest.substr(0, bar);
        fname = part_path(zip, p.name);
        for (RepeatCount i{}; i < p.common.repeat; i = i + RepeatCount{1}) {
            result = stream_file(fname, p.crc, p.offset, p.length, p.common.map);
            if (!result)
                break;
        }
        if (result || sized_out_)
            break;
        if (bar == std::string_view::npos)
            break;
        rest.remove_prefix(bar + 1);
    }
    if (!result && !sized_out_) {
        std::string msg = fname;
        msg.push_back('\n');
        msg.append(p.name);
        msg.append(" not found");
        if (msg.size() > kErrorCap)
            msg.resize(kErrorCap);
        clear_error();
        error_ = std::move(msg);
    }
    return result;
}

void RomSetAssembler::text_part(const TextPart& t) {
    const auto bin = std::make_shared<const std::vector<std::uint8_t>>(hex_bytes(t.text));
    for (RepeatCount i{}; i < t.common.repeat; i = i + RepeatCount{1}) {

        (void)(planning_ ? plan_text(bin, t.common.map) : scatter(*bin, t.common.map));
    }
}

bool RomSetAssembler::fill_run(const TextPart& t) {

    if (t.common.repeat > kFillRepeatCap
        || inside_ilv_
        || unitlen_ != UnitLen{}
        || !t.common.map.plain())
        return false;
    if (hw_ > romlen_[LaneIndex{}])
        return false;
    std::vector<std::uint8_t> pat = hex_bytes(t.text);
    const std::optional<RomLength> len = romlen_[LaneIndex{}]
                                             .repeats_within(kStagedRomMax, pat, t.common.repeat);
    if (!len)
        return false;
    if (len->empty())
        return true;
    if (!ensure_writable(dense_at(LaneIndex{})))
        return true;
    runs_.push_back(FillRun{.at = romlen_[LaneIndex{}], .len = *len, .pattern = std::move(pat)});
    if (planning_) {
        digest_.emplace_back(runs_.back());
        plan_.items.emplace_back(runs_.back());
    } else {
        md5_fill(runs_.back());
    }
    romlen_[LaneIndex{}] += *len;
    run_total_ += *len;
    hw_ = romlen_[LaneIndex{}];
    return true;
}

void RomSetAssembler::md5_fill(const FillRun& run) {
    const std::vector<std::uint8_t>& pat = run.pattern;
    std::vector<std::uint8_t> tile;
    tile.reserve(std::max<std::size_t>(1, kFillTileBytes / pat.size()) * pat.size());
    while (tile.size() + pat.size() <= tile.capacity()) {
        tile.insert(tile.end(), pat.begin(), pat.end());
    }
    for (RomLength left = run.len; !left.empty();) {
        const std::span<const std::uint8_t> k = infra::affine::first_clamped(
            std::span<const std::uint8_t>(tile),
            left
        );
        md5_.update(k);
        left -= RomLength::of(k);
    }
}

bool RomSetAssembler::flatten_runs() {

    const RomOffset dense = extent() + run_total_;
    if (dense > kStagedRomMax)
        return refuse_size();
    if (planning_) {

        plan_extent_ = dense;
        romblkl_ = dense;
        runs_.clear();
        run_total_ = RomLength{};
        return true;
    }
    RomBytes flat;
    if (!flat.resize(dense))
        return refuse_size();
    RomOffset w{};
    RomOffset from{};
    for (const FillRun& r : runs_) {
        const RomOffset dense_at = r.at - (w - from);
        flat.write(w, infra::affine::first_clamped(data_.from(from), dense_at - from));
        w += dense_at - from;
        from = dense_at;
        r.write_to(flat.slice(w, r.len), RomLength{});
        w += r.len;
    }
    flat.write(w, data_.from(from));
    data_ = std::move(flat);
    romblkl_ = data_.extent();
    runs_.clear();
    run_total_ = RomLength{};
    return true;
}

void RomSetAssembler::patch_node(const Patch& p) {
    const std::vector<std::uint8_t> bin = hex_bytes(p.text);

    const std::span<const std::uint8_t> patch = std::span(bin).first(
        static_cast<std::uint16_t>(bin.size())
    );
    const std::optional<RomOffset> at = p.address;
    if (!at)
        return;
    const RomOffset end = *at + RomLength::of(patch);
    if (end > romlen_[LaneIndex{}])
        return;

    RomLength skipped{};
    {
        bool overlaps = false;
        for (const FillRun& r : runs_) {
            if (r.end() <= *at) {
                skipped += r.len;
                continue;
            }
            if (end > r.at)
                overlaps = true;
            break;
        }
        if (overlaps) {
            if (!flatten_runs())
                return;
            skipped = RomLength{};
        }
    }
    if (planning_) {
        const RomLength n = RomLength::of(patch).min((*at - skipped).span_to(extent()));
        const std::span<const std::uint8_t> laid = infra::affine::first_clamped(patch, n);
        plan_.items.emplace_back(
            RomPlan::Patch{.at = *at, .bytes = {laid.begin(), laid.end()}, .xor_op = p.xor_op}
        );
        return;
    }
    const std::span<std::uint8_t> dst = data_.slice(*at - skipped, RomLength::of(patch));
    if (!p.xor_op) {
        std::ranges::copy(patch.first(dst.size()), dst.begin());
    } else {
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] ^= patch[i];
        }
    }
}

void RomSetAssembler::interleave_start(const InterleaveStart& start) noexcept {
    inside_ilv_ = true;

    unitlen_ = UnitLen::of_interleave(start.widths).value_or(UnitLen{});
    for (const LaneIndex lane : kLanesAfterFirst) {
        romlen_[lane] = romlen_[LaneIndex{}];
    }
}

void RomSetAssembler::interleave_end() noexcept {
    inside_ilv_ = false;
    unitlen_ = UnitLen{};
}

bool RomSetAssembler::grow_for(LaneIndex lane, RomLength reserve) {
    const RomOffset at = dense_at(lane);
    if (at + reserve > romblkl_) {
        const RomLength overshoot = at + reserve > romblkl_ + kRomGrowBlock ? reserve : RomLength{};
        const RomOffset want = romblkl_ + overshoot + kRomGrowBlock;
        if (want > kStagedRomMax)
            return refuse_size();
        romblkl_ = want;

        if (!resize_to(romblkl_))
            return refuse_size();
    }
    return true;
}

bool RomSetAssembler::ensure_writable(RomOffset hi) {
    if (hi <= extent())
        return true;
    RomOffset blk = romblkl_;
    while (blk < hi) {
        blk += kRomGrowBlock;
    }
    if (blk > kStagedRomMax)
        return refuse_size();
    romblkl_ = blk;
    if (!resize_to(blk))
        return refuse_size();
    return true;
}

bool RomSetAssembler::resize_to(RomOffset end) {
    if (!planning_)
        return data_.resize(end).has_value();
    plan_extent_ = end;
    return true;
}

RomOffset RomSetAssembler::extent() const noexcept {
    return planning_ ? plan_extent_ : data_.extent();
}

bool RomSetAssembler::refuse_size() {
    if (sized_out_)
        return false;
    ++repeat_refusals_;
    if (error_empty())
        error_ = "rom over size cap";
    sized_out_ = true;
    return false;
}

std::optional<RomSetAssembler::Placement> RomSetAssembler::place(RomLength chunk, MapWord map) {
    const std::optional<MapWalk> walk = map.walk(unitlen_);
    if (!walk)
        return std::nullopt;
    const LaneIndex lane = walk->lane;

    assert(runs_.empty() || romlen_[lane] >= runs_.back().end());
    const RomOffset base = dense_at(lane);
    if (!grow_for(lane, unitlen_.span_of(chunk)))
        return std::nullopt;
    const LaneWrites& writes = walk->writes;
    if (!chunk.empty()) {

        const RomOffset hi = base + writes.reach(chunk, unitlen_);
        if (!ensure_writable(hi))
            return std::nullopt;
        hw_ = std::max(hw_, hi + run_total_);
    }
    const Placement at{.at = romlen_[lane], .writes = writes};

    romlen_[lane] += chunk.units_covering(writes.per_unit()).times(unitlen_.bytes());
    return at;
}

bool RomSetAssembler::scatter(std::span<const std::uint8_t> src, MapWord map) {

    md5_.update(src);
    const std::optional<Placement> pl = place(RomLength::of(src), map);
    if (!pl)
        return false;
    RomOffset at = pl->at - run_total_;
    for (auto next = src.begin(); next != src.end();) {
        for (std::size_t i = 0; i < pl->writes.size() && next != src.end(); ++i) {
            data_[at + pl->writes.offset(i)] = *next++;
        }
        at += unitlen_.bytes();
    }
    return true;
}

bool RomSetAssembler::plan_text(
    const std::shared_ptr<const std::vector<std::uint8_t>>& bin,
    MapWord map
) {
    const RomLength len = RomLength::of(*bin);
    digest_.emplace_back(RomPlan::Segment{.text = bin, .len = len});
    const std::optional<Placement> pl = place(len, map);
    if (!pl)
        return false;
    if (len.empty())
        return true;
    plan_.items.emplace_back(
        RomPlan::Segment{
            .text = bin,
            .base = pl->at,
            .unit = unitlen_,
            .map = map,
            .chunk = len,
            .len = len
        }
    );
    return true;
}

bool RomSetAssembler::plan_member(
    const std::string& fname,
    svc::Crc32 crc,
    proto::FileOffset from,
    RomLength bytes,
    MapWord map
) {
    constexpr RomLength kChunk{kStreamChunkBytes};
    std::optional<RomPlan::Segment> seg;
    bool ok = true;
    for (RomLength done{}; done != bytes;) {
        const RomLength chunk = (bytes - done).min(kChunk);
        digest_member(fname, crc, from + done.file_size(), chunk);
        const std::optional<Placement> pl = place(chunk, map);
        if (!pl) {
            ok = false;
            break;
        }
        if (!seg) {
            seg = RomPlan::Segment{
                .path = fname,
                .crc = crc,
                .from = from + done.file_size(),
                .base = pl->at,
                .unit = unitlen_,
                .map = map,
                .chunk = kChunk
            };
        }
        seg->len += chunk;
        done += chunk;
    }
    if (seg)
        plan_.items.emplace_back(std::move(*seg));
    return ok;
}

void RomSetAssembler::digest_member(
    const std::string& fname,
    svc::Crc32 crc,
    proto::FileOffset from,
    RomLength len
) {
    if (!digest_.empty()) {
        auto* last = std::get_if<RomPlan::Segment>(&digest_.back());
        if (last != nullptr
            && !last->text
            && last->path == fname
            && last->crc == crc
            && last->from + last->len.file_size() == from) {
            last->len += len;
            return;
        }
    }
    digest_.emplace_back(RomPlan::Segment{.path = fname, .crc = crc, .from = from, .len = len});
}

void RomSetAssembler::digest_pass() {
    std::array<std::uint8_t, kStreamChunkBytes> buf{};
    for (const RomPlan::Item& piece : digest_) {
        if (const auto* run = std::get_if<FillRun>(&piece)) {
            md5_fill(*run);
            continue;
        }
        const auto* seg = std::get_if<RomPlan::Segment>(&piece);
        if (seg == nullptr)
            continue;
        if (seg->text) {
            md5_.update(*seg->text);
            continue;
        }
        auto f = vfs_.open_zip_by_crc(seg->path, seg->crc);
        for (RomLength done{}; done != seg->len;) {
            const std::span<std::uint8_t> chunk = infra::affine::first_clamped(
                std::span<std::uint8_t>(buf),
                seg->len - done
            );
            std::ranges::fill(chunk, 0);
            if (f)
                (void)(*f)->read_at(seg->from + done.file_size(), std::as_writable_bytes(chunk));
            md5_.update(chunk);
            done += RomLength::of(chunk);
        }
    }
}

bool RomSetAssembler::stream_file(
    const std::string& fname,
    svc::Crc32 crc,
    std::optional<RomOffset> start,
    std::optional<RomLength> length,
    MapWord map
) {
    auto f = vfs_.open_zip_by_crc(fname, crc);
    if (!f)
        return false;
    const Ex<proto::FileSize> sz = (*f)->size();
    if (!sz)
        return false;
    const RomLength size = RomLength::of(*sz);

    RomLength off = size;
    if (start) {
        off = start->distance_from_origin();
        if (off > size)
            off = size;
    }
    RomLength bytes2send = size - off;
    if (length && !length->empty())
        bytes2send = bytes2send.min(*length);
    if (planning_) {
        return plan_member(fname, crc, proto::kFileStart + off.file_size(), bytes2send, map);
    }
    std::array<std::uint8_t, kStreamChunkBytes> buf{};
    proto::FileOffset at = proto::kFileStart + off.file_size();
    while (bytes2send != RomLength{}) {
        const std::span<std::uint8_t> chunk = infra::affine::first_clamped(
            std::span<std::uint8_t>(buf),
            bytes2send
        );

        std::ranges::fill(chunk, 0);
        auto rd = (*f)->read_at(at, std::as_writable_bytes(chunk));
        (void)rd;
        if (!scatter(chunk, map))
            return false;
        bytes2send -= RomLength::of(chunk);
        at += proto::FileSize::of(std::span<const std::uint8_t>(chunk));
    }
    return true;
}

std::string RomSetAssembler::part_path(std::string_view zip, std::string_view member) const {
    std::string p(mame_root_);
    if (!zip.empty() && zip.front() == '/') {
        p.append(zip);
    } else {
        p.append("/mame/");
        p.append(zip);
    }
    p.push_back('/');
    p.append(member);
    return p;
}

AssembledRom RomSetAssembler::finish(const Rom& rom) {
    AssembledRom out;
    out.index = rom.index;
    out.address = rom.address;
    if (!rom.closed)
        return out;

    if (planning_ && !(rom.md5.empty() || infra::ieq(rom.md5, "none")) && !rom.zips.empty()) {
        digest_pass();
    }

    const Md5Hex hex = md5_.hex_str();
    const bool same = rom.zips.empty() || infra::ieq(rom.md5, hex.view());
    const bool none = rom.md5.empty() || infra::ieq(rom.md5, "none");
    if (!none) {
        if (!same) {
            if (error_empty()) {
                error_.assign("md5 mismatch for rom ");
                if (rom.index) {
                    error_.append(rom.index->text().view());
                } else {
                    error_.append("out of range");
                }
            }
        } else if (rom.index == proto::WideIoIndex{}) {
            validrom0_ = true;
            clear_error();
        }
    }
    if ((same || none)
        && !sized_out_
        && romlen_[LaneIndex{}] > RomOffset{}
        && (extent() > RomOffset{} || !runs_.empty())) {
        out.send = true;
        if (planning_) {
            plan_.length = romlen_[LaneIndex{}] - RomOffset{};
            out.plan = std::move(plan_);
            return out;
        }

        (void)data_.resize(dense_at(LaneIndex{}));
        out.bytes = std::move(data_);
        out.runs = std::move(runs_);
    }
    return out;
}

}
