// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/save_partition.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

struct PartitionHit {
    std::uint8_t index = 0;
    std::uint64_t offset = 0;
};

enum class SaveNaming : std::uint8_t {
    PerDiscFnv1a,
    PerCore,
    PerCoreIndexed,

    PerImagePartition,

};

struct NvramPolicy {
    SaveNaming naming = SaveNaming::PerCore;
    std::string_view prefix;
    std::string_view directory;
    std::string_view extension = "nvr";
    bool force_sd_root = false;
};

inline constexpr std::size_t kMaxSavePathChars = 255;
using SavePath = FixedStr<kMaxSavePathChars + 1, StrFit::Reject>;
using Fnv1aHex = FixedStr<17, StrFit::Reject>;

[[nodiscard]] Fnv1aHex fnv1a64_hex(std::string_view basename);

[[nodiscard]] SavePath pick_disc_path(const svc::Vfs& vfs, const SavePath& own,
                                      const SavePath& stock, std::uint64_t length);

class NvramStore {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxPartitions = 6;
    static constexpr std::size_t kMaxImageChars = 255;

    [[nodiscard]] static Ex<NvramStore> load(NvramPolicy policy, std::string_view image_basename,
                                             std::span<const SavePartition> declared);

    [[nodiscard]] Ex<void> relayout(std::span<const std::uint32_t> lengths);

    std::string_view image() const noexcept { return image_.view(); }
    std::span<const SavePartition> partitions() const noexcept { return {table_, count_}; }

    [[nodiscard]] Ex<SavePath> path_for(std::string_view partition) const;

    [[nodiscard]] Ex<SavePath> stock_path_for(std::string_view partition) const;

    [[nodiscard]] Ex<SavePartition> partition_view(std::string_view partition) const;

    [[nodiscard]] Ex<PartitionHit> resolve(std::uint64_t absolute_offset) const;

private:
    NvramStore() = default;

    [[nodiscard]] Ex<SavePath> per_disc_path(const SavePartition& p, bool stock) const;
    [[nodiscard]] Ex<SavePath> per_core_path(const SavePartition& p, bool indexed) const;
    [[nodiscard]] Ex<SavePath> per_image_path(const SavePartition& p) const;

    NvramPolicy policy_{};
    FixedStr<kMaxImageChars + 1, StrFit::Reject> image_{};
    SavePartition table_[kMaxPartitions]{};
    std::uint8_t count_ = 0;
};

}  // namespace mister::cores
