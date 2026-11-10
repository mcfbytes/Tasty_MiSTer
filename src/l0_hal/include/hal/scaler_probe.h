// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/scaler_buffers.h"
#include "hal/scaler_header.h"
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"

namespace mister::hal {

enum class ScalerStride : std::uint8_t { Large, Small };

[[nodiscard]] constexpr std::size_t stride_bytes(ScalerStride stride) noexcept {
    return stride == ScalerStride::Large ? ScalerBuffers::kStrideLarge
                                         : ScalerBuffers::kStrideSmall;
}

namespace detail {

[[nodiscard]] constexpr std::uint8_t stride_mib(ScalerStride stride) noexcept {
    constexpr unsigned kMibShift = 20;
    return static_cast<std::uint8_t>(stride_bytes(stride) >> kMibShift);
}

}

using detail::stride_mib;

class ScalerProbe {
    TASTY_SEAT_EXEMPT(component);

public:
    using BufMask = std::array<bool, ScalerBuffers::kBuffers>;
    static_assert(ScalerBuffers::kBuffers == 3, "kAllLive spells out every buffer");
    static constexpr BufMask kAllLive{true, true, true};
    static constexpr std::chrono::nanoseconds kProbeNs{std::chrono::milliseconds{250}};

    struct Pick {
        std::size_t buf = 0;
        HeaderCtr::Lag lag{};
        bool woven = false;
        std::size_t prev = ScalerBuffers::kBuffers;
    };

    enum class Verdict : std::uint8_t { Pending, Large, LargeLowlat, Small, Dead };

    struct ScalerLayout {
        ScalerStride stride = ScalerStride::Large;
        bool lowlat = false;
        friend constexpr bool operator==(const ScalerLayout&, const ScalerLayout&) = default;
    };

    [[nodiscard]] static constexpr std::optional<ScalerLayout> layout_of(Verdict verdict) noexcept {
        switch (verdict) {
            case Verdict::Large:
                return ScalerLayout{ScalerStride::Large, false};
            case Verdict::LargeLowlat:
                return ScalerLayout{ScalerStride::Large, true};
            case Verdict::Small:
                return ScalerLayout{ScalerStride::Small, false};
            case Verdict::Pending:
            case Verdict::Dead:
                return std::nullopt;
        }
        return std::nullopt;
    }

    void restart(os::RawMonotonicClock::time_point now) noexcept;
    void read(const ScalerBuffers& window) noexcept;
    [[nodiscard]] Verdict decide(os::RawMonotonicClock::time_point now) noexcept;
    [[nodiscard]] BufMask moved(ScalerStride stride) const noexcept;

    [[nodiscard]] static std::optional<Pick> pick_complete(
        const std::array<ScalerHeader, ScalerBuffers::kBuffers>& h,
        const BufMask& live = kAllLive
    ) noexcept;

    [[nodiscard]] static bool port_stuck(ScalerBuffers& window) noexcept;

    [[nodiscard]] static std::optional<Verdict> still(
        const ScalerBuffers& window,
        std::optional<ScalerStride> known = std::nullopt
    ) noexcept;

private:
    struct ProbeBuffer {
        HeaderCtr first{};
        bool seen = false;
        bool moved = false;
        bool triple = false;
    };

    struct ProbeSide {
        ScalerStride stride;
        std::array<ProbeBuffer, ScalerBuffers::kBuffers> buffers{};
    };

    [[nodiscard]] const ProbeSide& side_of(ScalerStride stride) const noexcept {
        return stride == ScalerStride::Large ? sides_[0] : sides_[1];
    }

    std::array<ProbeSide, 2> sides_{ProbeSide{ScalerStride::Large}, ProbeSide{ScalerStride::Small}};
    os::RawMonotonicClock::time_point start_{};
};

static_assert(!ScalerProbe::layout_of(ScalerProbe::Verdict::Pending).has_value());
static_assert(!ScalerProbe::layout_of(ScalerProbe::Verdict::Dead).has_value());
static_assert(
    ScalerProbe::layout_of(ScalerProbe::Verdict::Large).value()
    == ScalerProbe::ScalerLayout{ScalerStride::Large, false}
);
static_assert(
    ScalerProbe::layout_of(ScalerProbe::Verdict::LargeLowlat).value()
    == ScalerProbe::ScalerLayout{ScalerStride::Large, true}
);
static_assert(
    ScalerProbe::layout_of(ScalerProbe::Verdict::Small).value()
    == ScalerProbe::ScalerLayout{ScalerStride::Small, false}
);

}
