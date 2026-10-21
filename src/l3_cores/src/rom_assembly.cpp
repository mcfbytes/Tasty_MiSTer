// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_set_assembler.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "cores/assembled_rom.h"
#include "cores/mra_doc.h"
#include "cores/rom.h"
#include "cores/rom_node.h"
#include "infra/ascii.h"

namespace mister::cores::mra {

RomSetAssembler::RomSetAssembler(const svc::Vfs& vfs, std::string_view mame_root)
    : vfs_(vfs),
      mame_root_(mame_root) {}

void RomSetAssembler::rom_start() noexcept {
    data_.clear();
    romblkl_ = 0;
    romlen_.fill(0);
    unitlen_ = 1;
    inside_ilv_ = false;
    md5_ = Md5{};
    error_overwritten_ = false;
    runs_.clear();
    run_total_ = 0;
    hw_ = 0;
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

    if (!inside_ilv_ && n.map != 0) {
        const std::uint32_t digits = (n.map_digits > 8u) ? 8u : n.map_digits;
        unitlen_ = static_cast<int>(digits);
        for (std::size_t i = 1; i < kRomLanes; ++i) {
            romlen_[i] = romlen_[0];
        }
    }

    if (rom.index == 0 && validrom0_)
        return;
    bool ran = false;
    if (n.repeat > kRepeatCap && n.kind == RomNodeKind::TextPart)
        ran = fill_run(n);
    if (ran) {
        if (!inside_ilv_)
            unitlen_ = 1;
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
        unitlen_ = 1;
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
            result = stream_file(fname, n.crc, n.offset, length, n.map);
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
        (void)scatter(bin.data(), static_cast<int>(bin.size()), n.map);
    }
}

bool RomSetAssembler::fill_run(const RomNode& n) {

    if (n.repeat > kFillRepeatCap || inside_ilv_ || unitlen_ != 1 || n.map > 1)
        return false;
    if (hw_ > romlen_[0])
        return false;
    std::vector<std::uint8_t> pat = hex_bytes(n.text);
    const std::uint64_t len = static_cast<std::uint64_t>(n.repeat) * pat.size();
    if (static_cast<std::uint64_t>(romlen_[0]) + len > kStagedRomMax)
        return false;
    if (len == 0)
        return true;
    if (!ensure_writable(static_cast<std::size_t>(romlen_[0] - run_total_)))
        return true;

    constexpr std::size_t kTile = 64 * 1024;
    std::vector<std::uint8_t> tile;
    tile.reserve(std::max<std::size_t>(1, kTile / pat.size()) * pat.size());
    while (tile.size() + pat.size() <= tile.capacity()) {
        tile.insert(tile.end(), pat.begin(), pat.end());
    }
    std::uint64_t left = len;
    while (left != 0) {
        const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(left, tile.size()));
        md5_.update(std::span<const std::uint8_t>(tile.data(), k));
        left -= k;
    }
    runs_.push_back(
        FillRun{
            .at = static_cast<std::uint32_t>(romlen_[0]),
            .len = static_cast<std::uint32_t>(len),
            .pattern = std::move(pat)
        }
    );
    romlen_[0] += static_cast<int>(len);
    run_total_ += static_cast<int>(len);
    hw_ = romlen_[0];
    return true;
}

bool RomSetAssembler::flatten_runs() {

    const std::size_t dense = data_.size() + static_cast<std::size_t>(run_total_);
    if (dense > kStagedRomMax)
        return refuse_size();
    RomBytes flat;
    if (!flat.resize(dense))
        return refuse_size();
    std::size_t w = 0;
    std::size_t from = 0;
    for (const FillRun& r : runs_) {
        const std::size_t dense_at = r.at - (w - from);
        std::memcpy(flat.data() + w, data_.data() + from, dense_at - from);
        w += dense_at - from;
        from = dense_at;
        for (std::uint32_t i = 0; i < r.len; ++i) {
            flat[w + i] = r.pattern[i % r.pattern.size()];
        }
        w += r.len;
    }
    std::memcpy(flat.data() + w, data_.data() + from, data_.size() - from);
    data_ = std::move(flat);
    romblkl_ = static_cast<int>(data_.size());
    runs_.clear();
    run_total_ = 0;
    return true;
}

void RomSetAssembler::patch_node(const RomNode& n) {
    const std::vector<std::uint8_t> bin = hex_bytes(n.text);

    const std::uint16_t len = static_cast<std::uint16_t>(bin.size());
    if (n.offset < 0)
        return;
    if (static_cast<std::int64_t>(n.offset) + len > romlen_[0])
        return;

    std::uint64_t skipped = 0;
    {
        bool overlaps = false;
        for (const FillRun& r : runs_) {
            const std::uint64_t end = static_cast<std::uint64_t>(r.at) + r.len;
            if (end <= static_cast<std::uint64_t>(n.offset)) {
                skipped += r.len;
                continue;
            }
            if (static_cast<std::uint64_t>(n.offset) + len > r.at)
                overlaps = true;
            break;
        }
        if (overlaps) {
            if (!flatten_runs())
                return;
            skipped = 0;
        }
    }
    const std::size_t off = static_cast<std::size_t>(n.offset) - static_cast<std::size_t>(skipped);
    if (!n.xor_op) {
        for (std::size_t i = 0; i < len; ++i) {
            data_[off + i] = bin[i];
        }
    } else {
        for (std::size_t i = 0; i < len; ++i) {
            data_[off + i] ^= bin[i];
        }
    }
}

void RomSetAssembler::interleave_start(const RomNode& n) noexcept {
    inside_ilv_ = true;
    bool valid = true;
    if (n.input != 8)
        valid = false;
    if (n.output < 8 || n.output > 64 || (n.output & 7) != 0)
        valid = false;
    if (n.output < n.input)
        valid = false;
    unitlen_ = (n.input != 0) ? n.output / n.input : 1;
    if (unitlen_ < 0 || unitlen_ > 8)
        valid = false;
    if (!valid)
        unitlen_ = 1;
    for (std::size_t i = 1; i < kRomLanes; ++i) {
        romlen_[i] = romlen_[0];
    }
}

void RomSetAssembler::interleave_end() noexcept {
    inside_ilv_ = false;
    unitlen_ = 1;
}

bool RomSetAssembler::grow_for(std::size_t idx, int chunk) {
    const int at = romlen_[idx] - run_total_;
    if (at + chunk > romblkl_) {
        const std::int64_t want = static_cast<std::int64_t>(romblkl_)
            + (at + chunk > romblkl_ + kRomGrowBlock ? chunk : 0)
            + kRomGrowBlock;
        if (want > static_cast<std::int64_t>(kStagedRomMax))
            return refuse_size();
        romblkl_ = static_cast<int>(want);

        if (!data_.resize(static_cast<std::size_t>(romblkl_)))
            return refuse_size();
    }
    return true;
}

bool RomSetAssembler::ensure_writable(std::size_t hi) {
    if (hi <= data_.size())
        return true;
    std::size_t blk = static_cast<std::size_t>(romblkl_);
    while (blk < hi) {
        blk += kRomGrowBlock;
    }
    if (blk > kStagedRomMax)
        return refuse_size();
    romblkl_ = static_cast<int>(blk);
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

int RomSetAssembler::scatter(const std::uint8_t* src, int chunk, std::uint32_t map) {

    md5_.update(std::span<const std::uint8_t>(src, static_cast<std::size_t>(chunk)));
    if (map == 0)
        map = 1;
    std::size_t idx = 0;
    std::uint32_t map_reg = map;
    for (int i = 0; i < unitlen_; ++i) {
        if ((map_reg & 0xFu) != 0)
            break;
        map_reg >>= 4;
        ++idx;
    }
    if (idx >= static_cast<std::size_t>(unitlen_))
        return 0;

    assert(runs_.empty() || romlen_[idx] >= static_cast<int>(runs_.back().at + runs_.back().len));
    const int base = romlen_[idx] - run_total_;
    if (!grow_for(idx, chunk * unitlen_))
        return 0;
    std::array<std::uint8_t, kRomLanes> offsets{};
    int bytes_in_iter = 0;
    map_reg = map;
    bool first = true;
    int gaps = 0;
    for (int i = 0; i < unitlen_; ++i) {
        if ((map_reg & 0xFu) != 0) {
            offsets[static_cast<std::size_t>(bytes_in_iter)] = static_cast<std::uint8_t>(
                static_cast<int>(idx) + static_cast<int>(map_reg & 0xFu) - 1 + gaps
            );
            ++bytes_in_iter;
            first = false;
        } else if (!first) {
            ++gaps;
        }
        map_reg >>= 4;
    }
    if (chunk > 0) {

        int max_off = 0;
        for (int i = 0; i < bytes_in_iter; ++i) {
            max_off = std::max(max_off, static_cast<int>(offsets[static_cast<std::size_t>(i)]));
        }
        const int units = (chunk + bytes_in_iter - 1) / bytes_in_iter;
        const std::size_t hi = static_cast<std::size_t>(base)
            + static_cast<std::size_t>(units - 1) * static_cast<std::size_t>(unitlen_)
            + static_cast<std::size_t>(max_off)
            + 1;
        if (!ensure_writable(hi))
            return 0;
        hw_ = std::max(hw_, static_cast<int>(hi) + run_total_);
    }
    std::size_t at = static_cast<std::size_t>(base);
    while (chunk > 0) {

        for (int i = 0; i < bytes_in_iter && chunk > 0; ++i) {
            data_[at + offsets[static_cast<std::size_t>(i)]] = *src++;
            --chunk;
        }
        romlen_[idx] += unitlen_;
        at += static_cast<std::size_t>(unitlen_);
    }
    return 1;
}

int RomSetAssembler::stream_file(
    const std::string& fname,
    std::uint32_t crc,
    std::int32_t start,
    int length,
    std::uint32_t map
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
    std::array<std::uint8_t, 8192> buf{};
    while (bytes2send != 0) {
        const int chunk = (bytes2send > buf.size()) ? static_cast<int>(buf.size())
                                                    : static_cast<int>(bytes2send);

        std::memset(buf.data(), 0, static_cast<std::size_t>(chunk));
        auto rd = (*f)->read_at(
            off,
            std::as_writable_bytes(
                std::span<std::uint8_t>(buf.data(), static_cast<std::size_t>(chunk))
            )
        );
        (void)rd;
        if (scatter(buf.data(), chunk, map) == 0)
            return 0;
        bytes2send -= static_cast<std::uint64_t>(chunk);
        off += static_cast<std::uint64_t>(chunk);
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
    if ((same || none) && !sized_out_ && romlen_[0] > 0 && (!data_.empty() || !runs_.empty())) {
        out.send = true;

        (void)data_.resize(static_cast<std::size_t>(romlen_[0] - run_total_));
        out.bytes = std::move(data_);
        out.runs = std::move(runs_);
    }
    return out;
}

}
