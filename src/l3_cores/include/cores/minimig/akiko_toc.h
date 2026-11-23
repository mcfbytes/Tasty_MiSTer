// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/minimig/akiko_subq_reply.h"
#include "cores/minimig/cd_disc.h"
#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc/q_channel.h"
#include "svc/msf.h"
#include "svc/types.h"

namespace mister::cores::minimig::akiko {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct PointCount;

struct PointIndex {
    std::uint8_t v = 0;
    using affine_count = PointCount;
    using affine_index = PointIndex;
    friend constexpr bool operator==(PointIndex, PointIndex) = default;
    friend constexpr auto operator<=>(PointIndex, PointIndex) = default;
};

struct PointCount {
    std::uint8_t v = 0;
    using affine_count = PointCount;
    friend constexpr bool operator==(PointCount, PointCount) = default;
    friend constexpr auto operator<=>(PointCount, PointCount) = default;
};

struct DripStep {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr PointIndex point() const noexcept {
        constexpr std::uint8_t kRepeat = 3;
        return PointIndex{static_cast<std::uint8_t>(v / kRepeat)};
    }

    [[nodiscard]] constexpr svc::Msf lead_in_time() const noexcept {
        constexpr std::uint8_t kMinute = 99;
        constexpr std::uint8_t kSecond = 24;
        constexpr std::uint32_t kPerSecond = svc::kFramesPerSecond;
        const std::uint32_t frame = static_cast<std::uint32_t>(v);
        return svc::Msf{
            proto::PackedBcd::unmasked_byte(kMinute),
            proto::PackedBcd::unmasked_byte(kSecond + frame / kPerSecond),
            proto::PackedBcd::unmasked_byte(frame % kPerSecond)
        };
    }

    [[nodiscard]] constexpr DripStep next() const noexcept {
        return DripStep{static_cast<std::uint8_t>(v + 1)};
    }

    friend constexpr bool operator==(DripStep, DripStep) = default;
};

class AkikoToc {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr std::size_t kMaxPoints = 32;

    static constexpr PointCount kPointLimit{static_cast<std::uint8_t>(kMaxPoints)};

    void build(std::span<const Track> rows) noexcept;
    void clear() noexcept;

    [[nodiscard]] constexpr PointCount points() const noexcept { return count_; }

    [[nodiscard]] constexpr const svc::disc::QChannel& point(PointIndex i) const noexcept {
        return infra::at_index(table_, i);
    }

    void arm() noexcept {
        push_ = count_ > PointCount{} ? std::optional<DripStep>{DripStep{}} : std::nullopt;
    }

    void disarm() noexcept { push_.reset(); }

    [[nodiscard]] bool dripping() const noexcept { return push_.has_value(); }

    [[nodiscard]] std::optional<SubqReply> next() noexcept;

private:
    void add_(svc::disc::QChannel row) noexcept;

    std::array<svc::disc::QChannel, kMaxPoints> table_{};
    PointCount count_{};
    std::optional<DripStep> push_{};
};

}
