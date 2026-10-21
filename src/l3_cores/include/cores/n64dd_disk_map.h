// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/n64dd_geometry.h"
#include "cores/n64dd_system_area.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores::n64dd {

class DiskMap {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    struct Step {
        std::uint16_t track = 0;
        std::uint16_t lba = 0;
        std::uint8_t head = 0;
        std::uint8_t flat_block = 0;
        std::uint8_t copy_bytes = 0;
        std::uint8_t zone_bytes = 0;
        bool good = false;
        bool bad_lba = false;
    };

    static constexpr std::size_t kMaxSteps = 2u * 2u * kTracksPerHead;

    [[nodiscard]] Ex<void> build(const SystemArea& area) noexcept;

    [[nodiscard]] std::span<const Step> steps() const noexcept { return {steps_.data(), count_}; }

    [[nodiscard]] std::uint32_t ram_bytes() const noexcept { return ram_ok_ ? ram_bytes_ : 0; }

    [[nodiscard]] std::uint8_t disk_type() const noexcept { return disk_type_; }

    template <class Fn>
    void for_each_ram_run(std::uint64_t ram_off, std::uint64_t len, Fn&& fn) const {
        if (!ram_ok_ || len == 0 || ram_off >= ram_bytes_)
            return;
        const std::uint64_t end = ram_off + len < ram_bytes_ ? ram_off + len : ram_bytes_;
        std::uint64_t at = ram_off;
        for (std::size_t k = ram_index_at_(ram_off); k < ram_count_ && at < end; ++k) {
            const Step& s = steps_[ram_step_[k]];
            const std::uint64_t base = ram_base_[k];
            for (std::uint32_t sec = 0; sec < kSectorsPerBlock && at < end; ++sec) {
                const std::uint64_t lo = base + std::uint64_t{sec} * s.zone_bytes;
                const std::uint64_t hi = lo + s.zone_bytes;
                if (hi <= at)
                    continue;
                const std::uint64_t to = hi < end ? hi : end;
                fn(flat_offset(s.track, s.head, s.flat_block, sec)
                       + static_cast<std::uint32_t>(at - lo),
                   static_cast<std::size_t>(at - ram_off),
                   static_cast<std::size_t>(to - at));
                at = to;
            }
        }
    }

private:
    [[nodiscard]] std::size_t ram_index_at_(std::uint64_t ram_off) const noexcept;

    std::array<Step, kMaxSteps> steps_{};
    std::array<std::uint16_t, kLbaCount> ram_step_{};
    std::array<std::uint32_t, kLbaCount> ram_base_{};
    std::size_t count_ = 0;
    std::size_t ram_count_ = 0;
    std::uint32_t ram_bytes_ = 0;
    std::uint8_t disk_type_ = 0;
    bool ram_ok_ = false;
};

}
