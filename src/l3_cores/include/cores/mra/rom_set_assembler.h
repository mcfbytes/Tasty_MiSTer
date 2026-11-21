// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/mra/file_part.h"
#include "cores/mra/fill_run.h"
#include "cores/mra/interleave_start.h"
#include "cores/md5.h"
#include "cores/mra/mra_lane.h"
#include "cores/mra/mra_lane_writes.h"
#include "cores/mra/mra_unit_len.h"
#include "cores/mra/mra_map_word.h"
#include "cores/mra/part_common.h"
#include "cores/mra/patch.h"
#include "cores/mra/rom_bytes.h"
#include "cores/mra/rom_node.h"
#include "cores/mra/rom_plan.h"
#include "cores/mra/text_part.h"
#include "cores/types.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores::mra {

struct Rom;
struct AssembledRom;

inline constexpr RomLength kRomGrowBlock{1024u * 1024u};
inline constexpr std::size_t kErrorCap = 1023;

inline constexpr std::size_t kStreamChunkBytes = 8192;

inline constexpr std::size_t kFillTileBytes = 64u * 1024u;

inline constexpr RepeatCount kRepeatCap{16384};

inline constexpr RepeatCount kFillRepeatCap{512 * 1024 * 1024};

inline constexpr RomOffset kStagedRomMax{512u * 1024u * 1024u};

class RomSetAssembler {
public:
    TASTY_SEAT_RESIDENT(Main);

    RomSetAssembler(const svc::Vfs& vfs, std::string_view mame_root);

    [[nodiscard]] AssembledRom assemble(const Rom& rom);

    [[nodiscard]] AssembledRom plan(const Rom& rom);

    [[nodiscard]] std::string_view error() const noexcept { return error_; }

    [[nodiscard]] bool error_overwritten() const noexcept { return error_overwritten_; }

    [[nodiscard]] bool valid_rom0() const noexcept { return validrom0_; }

    [[nodiscard]] std::uint32_t repeat_refusals() const noexcept { return repeat_refusals_; }

private:
    void rom_start() noexcept;
    void clear_error() noexcept;

    [[nodiscard]] bool error_empty() const noexcept { return error_.empty(); }

    void part_node(const Rom& rom, const FilePart& part);
    void part_node(const Rom& rom, const TextPart& part);
    bool file_part(const Rom& rom, const FilePart& part);
    void text_part(const TextPart& part);
    [[nodiscard]] bool fill_run(const TextPart& part);
    [[nodiscard]] bool flatten_runs();
    void patch_node(const Patch& patch);
    void interleave_start(const InterleaveStart& start) noexcept;
    void interleave_end() noexcept;

    [[nodiscard]] bool admit_part(const Rom& rom, const PartCommon& common);
    void refuse_repeat();
    [[nodiscard]] AssembledRom finish(const Rom& rom);

    [[nodiscard]] RomOffset dense_at(LaneIndex lane) const noexcept {
        return romlen_[lane] - run_total_;
    }

    [[nodiscard]] bool grow_for(LaneIndex lane, RomLength reserve);
    [[nodiscard]] bool ensure_writable(RomOffset hi);
    [[nodiscard]] bool refuse_size();

    struct Placement {
        RomOffset at{};
        LaneWrites writes{};
    };

    [[nodiscard]] std::optional<Placement> place(RomLength chunk, MapWord map);
    [[nodiscard]] bool scatter(std::span<const std::uint8_t> src, MapWord map);

    [[nodiscard]] bool plan_text(
        const std::shared_ptr<const std::vector<std::uint8_t>>& bin,
        MapWord map
    );
    [[nodiscard]] bool plan_member(
        const std::string& fname,
        svc::Crc32 crc,
        proto::FileOffset from,
        RomLength bytes,
        MapWord map
    );
    void digest_member(
        const std::string& fname,
        svc::Crc32 crc,
        proto::FileOffset from,
        RomLength len
    );
    void digest_pass();
    void md5_fill(const FillRun& run);
    [[nodiscard]] bool resize_to(RomOffset end);
    [[nodiscard]] RomOffset extent() const noexcept;
    [[nodiscard]] bool stream_file(
        const std::string& fname,
        svc::Crc32 crc,
        std::optional<RomOffset> start,
        std::optional<RomLength> length,
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

    bool planning_ = false;
    RomOffset plan_extent_{};
    RomPlan plan_;
    std::vector<RomPlan::Item> digest_;
};

}
