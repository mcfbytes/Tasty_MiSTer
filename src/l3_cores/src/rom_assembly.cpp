// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_set_assembler.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <span>

#include "cores/assembled_rom.h"
#include "cores/mra_doc.h"
#include "cores/rom.h"
#include "cores/rom_node.h"
#include "infra/ascii.h"

namespace mister::cores::mra {
namespace {}

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
}

void RomSetAssembler::clear_error() noexcept {
    error_.clear();
    error_overwritten_ = true;
}

AssembledRom RomSetAssembler::assemble(const Rom& rom) {
    rom_start();

    if (rom.index == 0 && !rom.zips.empty())
        clear_error();
    for (const RomNode& n : rom.nodes) {
        switch (n.kind) {
            case RomNodeKind::FilePart:
            case RomNodeKind::TextPart:
                part_node(rom, n);
                break;
            case RomNodeKind::Patch:
                patch_node(n);
                break;
            case RomNodeKind::InterleaveStart:
                interleave_start(n);
                break;
            case RomNodeKind::InterleaveEnd:
                interleave_end();
                break;
        }
    }
    return finish(rom);
}

void RomSetAssembler::part_node(const Rom& rom, const RomNode& n) {

    if (!inside_ilv_ && !n.map.is_none()) {
        unitlen_ = UnitLen::of_map_digits(n.map.digits);
        for (std::size_t i = 1; i < kRomLanes; ++i) {
            romlen_[LaneIndex::at(i)] = romlen_[LaneIndex{}];
        }
    }

    if (rom.index == 0 && validrom0_)
        return;
    bool ran = false;
    if (n.repeat > kRepeatCap && n.kind == RomNodeKind::TextPart)
        ran = fill_run(n);
    if (ran) {
        if (!inside_ilv_)
            unitlen_ = UnitLen::one();
        return;
    }
    if (n.repeat > kRepeatCap) {

        ++repeat_refusals_;
        if (error_empty())
            error_ = "part repeat over cap";
    } else if (n.kind == RomNodeKind::FilePart) {
        file_part(rom, n);
    } else {
        text_part(n);
    }
    if (!inside_ilv_)
        unitlen_ = UnitLen::one();
}

void RomSetAssembler::file_part(const Rom& rom, const RomNode& n) {
    const std::string& zlist = n.zips.empty() ? rom.zips : n.zips;
    const int length = (n.length > 0) ? n.length : 0;
    int result = 0;
    std::string fname;
    std::string_view rest = zlist;
    while (true) {
        const std::size_t bar = rest.find('|');
        const std::string_view zip = (bar == std::string_view::npos) ? rest : rest.substr(0, bar);
        fname = part_path(zip, n.name);
        for (int i = 0; i < n.repeat; ++i) {
            result = stream_file(fname, n.crc.v, n.offset, length, n.map);
            if (result == 0)
                break;
        }
        if (result != 0 || sized_out_)
            break;
        if (bar == std::string_view::npos)
            break;
        rest.remove_prefix(bar + 1);
    }
    if (result == 0 && !sized_out_) {
        std::string msg = fname;
        msg.push_back('\n');
        msg.append(n.name);
        msg.append(" not found");
        if (msg.size() > kErrorCap)
            msg.resize(kErrorCap);
        clear_error();
        error_ = std::move(msg);
    }
}

void RomSetAssembler::text_part(const RomNode& n) {
    const std::vector<std::uint8_t> bin = hex_bytes(n.text);
    for (int i = 0; i < n.repeat; ++i) {
        (void)scatter(bin, n.map);
    }
}

bool RomSetAssembler::fill_run(const RomNode& n) {

    if (n.repeat > kFillRepeatCap || inside_ilv_ || unitlen_ != UnitLen{} || !n.map.plain())
        return false;
    if (hw_ > romlen_[LaneIndex{}])
        return false;
    std::vector<std::uint8_t> pat = hex_bytes(n.text);
    const std::optional<RomLength> len = romlen_[LaneIndex{}]
                                             .repeats_within(kStagedRomMax, pat, n.repeat);
    if (!len)
        return false;
    if (len->empty())
        return true;
    if (!ensure_writable(romlen_[LaneIndex{}] - run_total_))
        return true;

    std::vector<std::uint8_t> tile;
    tile.reserve(std::max<std::size_t>(1, kFillTileBytes / pat.size()) * pat.size());
    while (tile.size() + pat.size() <= tile.capacity()) {
        tile.insert(tile.end(), pat.begin(), pat.end());
    }
    for (RomLength left = *len; !left.empty();) {
        const std::span<const std::uint8_t> k = left.first_of(std::span<const std::uint8_t>(tile));
        md5_.update(k);
        left -= RomLength::of(k);
    }
    runs_.push_back(FillRun{.at = romlen_[LaneIndex{}], .len = *len, .pattern = std::move(pat)});
    romlen_[LaneIndex{}] += *len;
    run_total_ += *len;
    hw_ = romlen_[LaneIndex{}];
    return true;
}

bool RomSetAssembler::flatten_runs() {

    const RomOffset dense = data_.extent() + run_total_;
    if (dense > kStagedRomMax)
        return refuse_size();
    RomBytes flat;
    if (!flat.resize(dense))
        return refuse_size();
    RomOffset w{};
    RomOffset from{};
    for (const FillRun& r : runs_) {
        const RomOffset dense_at = r.at - (w - from);
        flat.write(w, (dense_at - from).first_of(data_.from(from)));
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

void RomSetAssembler::patch_node(const RomNode& n) {
    const std::vector<std::uint8_t> bin = hex_bytes(n.text);

    const std::span<const std::uint8_t> patch = std::span(bin).first(
        static_cast<std::uint16_t>(bin.size())
    );
    const std::optional<RomOffset> at = RomOffset::of_attribute(n.offset);
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
    const std::span<std::uint8_t> dst = data_.slice(*at - skipped, RomLength::of(patch));
    if (!n.xor_op) {
        std::ranges::copy(patch.first(dst.size()), dst.begin());
    } else {
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] ^= patch[i];
        }
    }
}

void RomSetAssembler::interleave_start(const RomNode& n) noexcept {
    inside_ilv_ = true;

    unitlen_ = UnitLen::of_interleave(n.input, n.output).value_or(UnitLen{});
    for (std::size_t i = 1; i < kRomLanes; ++i) {
        romlen_[LaneIndex::at(i)] = romlen_[LaneIndex{}];
    }
}

void RomSetAssembler::interleave_end() noexcept {
    inside_ilv_ = false;
    unitlen_ = UnitLen{};
}

bool RomSetAssembler::grow_for(LaneIndex lane, RomLength reserve) {
    const RomOffset at = romlen_[lane] - run_total_;
    if (at + reserve > romblkl_) {
        const RomLength overshoot = at + reserve > romblkl_ + kRomGrowBlock ? reserve : RomLength{};
        const RomOffset want = romblkl_ + overshoot + kRomGrowBlock;
        if (want > kStagedRomMax)
            return refuse_size();
        romblkl_ = want;

        if (!data_.resize(romblkl_))
            return refuse_size();
    }
    return true;
}

bool RomSetAssembler::ensure_writable(RomOffset hi) {
    if (hi <= data_.extent())
        return true;
    RomOffset blk = romblkl_;
    while (blk < hi) {
        blk += kRomGrowBlock;
    }
    if (blk > kStagedRomMax)
        return refuse_size();
    romblkl_ = blk;
    if (!data_.resize(blk))
        return refuse_size();
    return true;
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

int RomSetAssembler::scatter(std::span<const std::uint8_t> src, MapWord map) {

    md5_.update(src);
    const std::optional<MapWalk> walk = map.walk(unitlen_);
    if (!walk)
        return 0;
    const LaneIndex lane = walk->lane;

    assert(runs_.empty() || romlen_[lane] >= runs_.back().end());
    const RomOffset base = romlen_[lane] - run_total_;
    const RomLength chunk = RomLength::of(src);
    if (!grow_for(lane, unitlen_.span_of(chunk)))
        return 0;
    const LaneWrites& writes = walk->writes;
    if (!chunk.empty()) {

        const RomOffset hi = base + writes.reach(chunk, unitlen_);
        if (!ensure_writable(hi))
            return 0;
        hw_ = std::max(hw_, hi + run_total_);
    }
    RomOffset at = base;
    for (auto next = src.begin(); next != src.end();) {

        for (std::size_t i = 0; i < writes.size() && next != src.end(); ++i) {
            data_[at + writes.offset(i)] = *next++;
        }
        romlen_[lane] += unitlen_.bytes();
        at += unitlen_.bytes();
    }
    return 1;
}

int RomSetAssembler::stream_file(
    const std::string& fname,
    std::uint32_t crc,
    std::int32_t start,
    int length,
    MapWord map
) {
    auto f = vfs_.open_zip_by_crc(fname, svc::Crc32{crc});
    if (!f)
        return 0;
    const Ex<proto::FileSize> sz = (*f)->size();
    if (!sz)
        return 0;
    const std::uint64_t size = sz->v;
    std::uint64_t off = 0;
    if (start != 0)
        off = static_cast<std::uint32_t>(start);
    if (off > size)
        off = size;
    std::uint64_t bytes2send = size - off;
    if (length > 0 && static_cast<std::uint64_t>(length) < bytes2send) {
        bytes2send = static_cast<std::uint64_t>(length);
    }
    std::array<std::uint8_t, kStreamChunkBytes> buf{};
    while (bytes2send != 0) {
        const std::span<std::uint8_t> chunk = std::span(buf).first(
            static_cast<std::size_t>(std::min<std::uint64_t>(bytes2send, buf.size()))
        );

        std::ranges::fill(chunk, 0);
        auto rd = (*f)->read_at(off, std::as_writable_bytes(chunk));
        (void)rd;
        if (scatter(chunk, map) == 0)
            return 0;
        bytes2send -= chunk.size();
        off += chunk.size();
    }
    return 1;
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

    const Md5Hex hex = md5_.hex_str();
    const bool same = rom.zips.empty() || infra::ieq(rom.md5, hex.view());
    const bool none = rom.md5.empty() || infra::ieq(rom.md5, "none");
    if (!none) {
        if (!same) {
            if (error_empty()) {
                error_ = "md5 mismatch for rom " + std::to_string(rom.index);
            }
        } else if (rom.index == 0) {
            validrom0_ = true;
            clear_error();
        }
    }
    if ((same || none)
        && !sized_out_
        && romlen_[LaneIndex{}] > RomOffset{}
        && (!data_.empty() || !runs_.empty())) {
        out.send = true;

        (void)data_.resize(romlen_[LaneIndex{}] - run_total_);
        out.bytes = std::move(data_);
        out.runs = std::move(runs_);
    }
    return out;
}

}
