// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/n64dd/n64dd_geometry.h"
#include "cores/n64dd/n64dd_system_area.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::n64dd {

class DiskMap {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    struct NddLba {
        TASTY_SEAT_EXEMPT(component);
        std::uint16_t v = 0;

        [[nodiscard]] constexpr bool at_or_above(NddLba other) const noexcept {
            return v >= other.v;
        }

        friend constexpr bool operator==(NddLba, NddLba) = default;
    };

    struct Step {
        FlatBlock where{};
        NddLba lba{};
        std::uint8_t copy_bytes = 0;
        std::uint8_t zone_bytes = 0;
        bool good = false;
        bool bad_lba = false;
    };

    static constexpr std::size_t kMaxSteps = 2u * 2u * kTracksPerHead;

    [[nodiscard]] static Ex<void> build_into(DiskMap& out, const SystemArea& area) noexcept;

    [[nodiscard]] std::span<const Step> steps() const noexcept { return {steps_.data(), count_}; }

    [[nodiscard]] proto::FileSize ram_bytes() const noexcept {
        return proto::FileSize{ram_ok_ ? infra::at_index(kRamBytes, disk_type_) : 0u};
    }

    [[nodiscard]] DiskType disk_type() const noexcept { return disk_type_; }

    template <class Fn>
    void for_each_ram_run(proto::FileOffset from, proto::FileSize len, Fn&& fn) const {
        const proto::FileSize total = ram_bytes();
        if (!ram_ok_ || len.empty() || !total.holds(from))
            return;
        const proto::FileOffset end = std::min(from + len, proto::kFileStart + total);
        proto::FileOffset at = from;
        for (std::size_t k = ram_index_at_(from); k < ram_count_ && at < end; ++k) {
            const RamRun& run = ram_runs_[k];
            const Step& s = steps_[run.step];
            const proto::FileSize sector{s.zone_bytes};
            for (std::uint32_t sec = 0; sec < kSectorsPerBlock && at < end; ++sec) {
                const proto::FileOffset lo = run.start() + sector * std::uint64_t{sec};
                const proto::FileOffset hi = lo + sector;
                if (hi <= at)
                    continue;
                const proto::FileOffset to = std::min(hi, end);
                const FlatOffset run_off = s.where.sector_offset(sec).advanced_by(
                    (at - lo).file_word<std::uint32_t>()
                );
                fn(run_off,
                   (at - from).file_word<std::size_t>(),
                   (to - at).file_word<std::size_t>());
                at = to;
            }
        }
    }

private:
    struct RamRun {
        std::uint32_t base = 0;
        std::uint16_t step = 0;

        [[nodiscard]] constexpr proto::FileOffset start() const noexcept {
            return proto::kFileStart + proto::FileSize{base};
        }
    };

    [[nodiscard]] std::size_t ram_index_at_(proto::FileOffset at) const noexcept;

    std::array<Step, kMaxSteps> steps_{};
    std::array<RamRun, kLbaCount> ram_runs_{};
    std::size_t count_ = 0;
    std::size_t ram_count_ = 0;
    DiskType disk_type_{};
    bool ram_ok_ = false;
};

}
