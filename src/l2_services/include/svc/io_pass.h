// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <ratio>
#include <type_traits>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kIoPassBuckets = 20;
inline constexpr std::chrono::nanoseconds kIoPassFirstBucketEdge{1024};

inline constexpr std::uint32_t kIoPassPercentileDenominator = 100;

using IoPassMicros = std::chrono::duration<std::uint32_t, std::micro>;

struct IoPassCount {
    std::uint32_t v = 0;
    using affine_count = IoPassCount;

    [[nodiscard]] constexpr bool is_multiple_of(IoPassCount every) const noexcept {
        return v % every.v == 0;
    }

    [[nodiscard]] constexpr IoPassCount p99_rank() const noexcept {
        return IoPassCount{v - v / kIoPassPercentileDenominator};
    }

    friend constexpr bool operator==(IoPassCount, IoPassCount) = default;
    friend constexpr auto operator<=>(IoPassCount, IoPassCount) = default;
};

static_assert(infra::affine::Count<IoPassCount>, "a pass count is a count");

inline constexpr IoPassCount kIoPassPublishEvery{256};

struct IoPass {
    IoPassCount passes{};
    std::chrono::nanoseconds max_ns{};
    std::array<IoPassCount, kIoPassBuckets> hist{};
    IoPassCount bulk_deferred{};
    IoPassCount bulk_run_max{};

    [[nodiscard]] constexpr IoPass with_pass(std::chrono::nanoseconds took) const noexcept {
        IoPass next = *this;
        next.passes += IoPassCount{1};
        std::size_t b = 0;
        for (std::chrono::nanoseconds edge = kIoPassFirstBucketEdge;
             took >= edge && b + 1 < kIoPassBuckets;
             edge += edge)
            ++b;
        next.hist[b] += IoPassCount{1};
        next.max_ns = std::max(max_ns, took);
        return next;
    }

    [[nodiscard]] constexpr IoPassMicros max_micros() const noexcept {
        return std::chrono::floor<IoPassMicros>(max_ns);
    }

    [[nodiscard]] constexpr IoPassMicros p99() const noexcept {
        if (passes == IoPassCount{})
            return IoPassMicros{};
        const IoPassCount want = passes.p99_rank();
        IoPassCount seen{};
        std::chrono::nanoseconds edge = kIoPassFirstBucketEdge;
        for (std::size_t b = 0; b + 1 < kIoPassBuckets; ++b, edge += edge) {
            seen += hist[b];
            if (seen >= want)
                break;
        }
        return std::chrono::ceil<IoPassMicros>(edge);
    }
};

static_assert(std::is_trivially_copyable_v<IoPass>);
static_assert(std::is_nothrow_default_constructible_v<IoPass>);

using IoPassCell = xthread::Telemetry<IoPass, SeatTag::Io>;

}
