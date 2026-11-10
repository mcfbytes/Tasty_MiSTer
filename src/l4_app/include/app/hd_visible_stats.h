// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/hd_visible_window.h"
#include "app/log_clock.h"
#include "infra/seat.h"

namespace mister::app {

class HdVisibleStats {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint64_t kWindowNs = 1'000'000'000;
    static constexpr std::array<StatusMicros, 16> kBucketUs{
        StatusMicros{1'000},
        StatusMicros{2'000},
        StatusMicros{3'000},
        StatusMicros{4'000},
        StatusMicros{6'000},
        StatusMicros{8'000},
        StatusMicros{12'000},
        StatusMicros{16'000},
        StatusMicros{24'000},
        StatusMicros{32'000},
        StatusMicros{48'000},
        StatusMicros{64'000},
        StatusMicros{96'000},
        StatusMicros{128'000},
        StatusMicros{256'000},
        StatusMicros::max()
    };

    constexpr void sample(StatusMicros span) noexcept {
        std::size_t b = 0;
        while (b + 1 < kBucketUs.size() && span > kBucketUs[b])
            ++b;
        ++hist_[b];
        if (n_ == 0 || span < min_)
            min_ = span;
        if (span > max_)
            max_ = span;
        ++n_;
    }

    constexpr void in_place() noexcept { ++inplace_; }

    constexpr void flipped() noexcept { ++flips_; }

    constexpr void backdrop() noexcept { ++bd_frames_; }

    constexpr void copied(std::size_t bytes) noexcept { bytes_ += bytes; }

    [[nodiscard]] constexpr std::optional<HdVisibleWindow> roll(std::uint64_t now) noexcept {
        if (start_ == 0)
            start_ = now == 0 ? 1 : now;
        if (now < start_ || now - start_ < kWindowNs)
            return std::nullopt;
        HdVisibleWindow w{};
        w.ask_count = svc::Tally{n_};
        w.ask_us_min = n_ == 0 ? StatusMicros::zero() : min_;
        w.ask_us_max = max_;
        w.ask_us_p50 = p50_();
        w.inplace = svc::Tally{inplace_};
        w.flips = svc::Tally{flips_};
        w.bd_frames = svc::Tally{bd_frames_};
        const std::uint64_t kb = bytes_ / 1024u;
        w.copy_kb = KibCount{sat_u32(kb)};
        *this = HdVisibleStats{};
        start_ = now == 0 ? 1 : now;
        return w;
    }

private:
    [[nodiscard]] constexpr StatusMicros p50_() const noexcept {
        if (n_ == 0)
            return StatusMicros::zero();
        const std::uint32_t want = (n_ + 1u) / 2u;
        std::uint32_t seen = 0;
        for (std::size_t b = 0; b < kBucketUs.size(); ++b) {
            seen += hist_[b];
            if (seen < want)
                continue;
            const StatusMicros edge = kBucketUs[b];
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
    StatusMicros min_{};
    StatusMicros max_{};
    std::uint32_t inplace_ = 0;
    std::uint32_t flips_ = 0;
    std::uint32_t bd_frames_ = 0;
};

}
