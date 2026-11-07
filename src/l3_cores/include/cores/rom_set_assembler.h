// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/fill_run.h"
#include "cores/md5.h"
#include "cores/mra_lane.h"
#include "cores/mra_lane_writes.h"
#include "cores/mra_unit_len.h"
#include "cores/mra_map_word.h"
#include "cores/rom_bytes.h"
#include "cores/types.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores::mra {

struct Rom;
struct RomNode;
struct AssembledRom;

inline constexpr RomLength kRomGrowBlock{1024u * 1024u};
inline constexpr std::size_t kErrorCap = 1023;

inline constexpr std::size_t kStreamChunkBytes = 8192;

inline constexpr std::size_t kFillTileBytes = 64u * 1024u;

inline constexpr std::int32_t kRepeatCap = 16384;

inline constexpr std::int32_t kFillRepeatCap = 512 * 1024 * 1024;

inline constexpr RomOffset kStagedRomMax{512u * 1024u * 1024u};

class RomSetAssembler {
public:
    TASTY_SEAT_RESIDENT(Main);

    RomSetAssembler(const svc::Vfs& vfs, std::string_view mame_root);

    [[nodiscard]] AssembledRom assemble(const Rom& rom);

    [[nodiscard]] std::string_view error() const noexcept { return error_; }

    [[nodiscard]] bool error_overwritten() const noexcept { return error_overwritten_; }

    [[nodiscard]] bool valid_rom0() const noexcept { return validrom0_; }

    [[nodiscard]] std::uint32_t repeat_refusals() const noexcept { return repeat_refusals_; }

private:
    void rom_start() noexcept;
    void clear_error() noexcept;

    [[nodiscard]] bool error_empty() const noexcept { return error_.empty(); }

    void part_node(const Rom& rom, const RomNode& n);
    void file_part(const Rom& rom, const RomNode& n);
    void text_part(const RomNode& n);
    [[nodiscard]] bool fill_run(const RomNode& n);
    [[nodiscard]] bool flatten_runs();
    void patch_node(const RomNode& n);
    void interleave_start(const RomNode& n) noexcept;
    void interleave_end() noexcept;
    [[nodiscard]] AssembledRom finish(const Rom& rom);

    [[nodiscard]] bool grow_for(LaneIndex lane, RomLength reserve);
    [[nodiscard]] bool ensure_writable(RomOffset hi);
    [[nodiscard]] bool refuse_size();
    [[nodiscard]] int scatter(std::span<const std::uint8_t> src, MapWord map);
    [[nodiscard]] int stream_file(
        const std::string& fname,
        std::uint32_t crc,
        std::int32_t start,
        int length,
        MapWord map
    );
    [[nodiscard]] std::string part_path(std::string_view zip, std::string_view member) const;

    const svc::Vfs& vfs_;
    std::string mame_root_;
    std::string error_;
    bool error_overwritten_ = false;
    bool validrom0_ = false;
    std::uint32_t repeat_refusals_ = 0;
    bool sized_out_ = false;

    RomBytes data_;
    infra::IndexedArray<RomOffset, kRomLanes, LaneIndex> romlen_{};
    RomOffset romblkl_{};
    UnitLen unitlen_{};
    bool inside_ilv_ = false;
    Md5 md5_;

    std::vector<FillRun> runs_;
    RomLength run_total_{};
    RomOffset hw_{};
};

}
