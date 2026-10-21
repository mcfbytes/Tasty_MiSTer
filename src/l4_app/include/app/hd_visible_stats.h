// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/hd_osd_status.h"
#include "infra/seat.h"

namespace mister::app {

class HdVisibleStats {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint64_t kWindowNs = 1'000'000'000;
    static constexpr std::array<std::uint32_t, 16> kBucketUs{
        1'000,
        2'000,
        3'000,
        4'000,
        6'000,
        8'000,
        12'000,
        16'000,
        24'000,
        32'000,
        48'000,
        64'000,
        96'000,
        128'000,
        256'000,
        0xffffffffu
    };

    constexpr void sample(std::uint32_t us) noexcept {
        std::size_t b = 0;
        while (b + 1 < kBucketUs.size() && us > kBucketUs[b])
            ++b;
        ++hist_[b];
        if (n_ == 0 || us < min_)
            min_ = us;
        if (us > max_)
            max_ = us;
        ++n_;
    }

    constexpr void in_place() noexcept { ++inplace_; }

    constexpr void flipped() noexcept { ++flips_; }

    constexpr void backdrop() noexcept { ++bd_frames_; }

    constexpr void copied(std::size_t bytes) noexcept { bytes_ += bytes; }

    constexpr bool roll(std::uint64_t now, HdOsdStatus& out) noexcept {
        if (start_ == 0)
            start_ = now == 0 ? 1 : now;
        if (now < start_ || now - start_ < kWindowNs)
            return false;
        out.ask_n = n_;
        out.ask_us_min = n_ == 0 ? 0 : min_;
        out.ask_us_max = max_;
        out.ask_us_p50 = p50_();
        out.inplace = inplace_;
        out.flips = flips_;
        out.bd_frames = bd_frames_;
        const std::uint64_t kb = bytes_ / 1024u;
        out.copy_kb = kb > 0xffffffffu ? 0xffffffffu : static_cast<std::uint32_t>(kb);
        *this = HdVisibleStats{};
        start_ = now == 0 ? 1 : now;
        return true;
    }

private:
    [[nodiscard]] constexpr std::uint32_t p50_() const noexcept {
        if (n_ == 0)
            return 0;
        const std::uint32_t want = (n_ + 1u) / 2u;
        std::uint32_t seen = 0;
        for (std::size_t b = 0; b < kBucketUs.size(); ++b) {
            seen += hist_[b];
            if (seen < want)
                continue;
            const std::uint32_t edge = kBucketUs[b];
            if (edge > max_)
                return max_;
            return edge < min_ ? min_ : edge;
        }
        return max_;
    }

    std::array<std::uint32_t, kBucketUs.size()> hist_{};
    std::uint64_t start_ = 0;
    std::uint64_t bytes_ = 0;
    std::uint32_t n_ = 0;
    std::uint32_t min_ = 0;
    std::uint32_t max_ = 0;
    std::uint32_t inplace_ = 0;
    std::uint32_t flips_ = 0;
    std::uint32_t bd_frames_ = 0;
};

}
