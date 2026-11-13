// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/n64dd_geometry.h"
#include "cores/n64dd_system_area.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

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

    [[nodiscard]] std::uint32_t ram_bytes() const noexcept {
        return ram_ok_ ? infra::at_index(kRamBytes, disk_type_) : 0;
    }

    [[nodiscard]] DiskType disk_type() const noexcept { return disk_type_; }

    template <class Fn>
    void for_each_ram_run(std::uint64_t ram_off, std::uint64_t len, Fn&& fn) const {
        const std::uint64_t total = ram_bytes();
        if (!ram_ok_ || len == 0 || ram_off >= total)
            return;
        const std::uint64_t end = ram_off + len < total ? ram_off + len : total;
        std::uint64_t at = ram_off;
        for (std::size_t k = ram_index_at_(ram_off); k < ram_count_ && at < end; ++k) {
            const RamRun& run = ram_runs_[k];
            const Step& s = steps_[run.step];
            const std::uint64_t base = run.base;
            for (std::uint32_t sec = 0; sec < kSectorsPerBlock && at < end; ++sec) {
                const std::uint64_t lo = base + std::uint64_t{sec} * s.zone_bytes;
                const std::uint64_t hi = lo + s.zone_bytes;
                if (hi <= at)
                    continue;
                const std::uint64_t to = hi < end ? hi : end;
                const FlatOffset run_off = s.where.sector_offset(sec).advanced_by(
                    static_cast<std::uint32_t>(at - lo)
                );
                fn(run_off,
                   static_cast<std::size_t>(at - ram_off),
                   static_cast<std::size_t>(to - at));
                at = to;
            }
        }
    }

private:
    struct RamRun {
        std::uint32_t base = 0;
        std::uint16_t step = 0;
    };

    [[nodiscard]] std::size_t ram_index_at_(std::uint64_t ram_off) const noexcept;

    std::array<Step, kMaxSteps> steps_{};
    std::array<RamRun, kLbaCount> ram_runs_{};
    std::size_t count_ = 0;
    std::size_t ram_count_ = 0;
    DiskType disk_type_{};
    bool ram_ok_ = false;
};

}
