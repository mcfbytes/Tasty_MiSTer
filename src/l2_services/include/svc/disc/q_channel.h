// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/seat.h"
#include "infra/span_cut.h"
#include "proto/types.h"
#include "svc/disc/q_control.h"
#include "svc/disc/toc_point.h"
#include "svc/disc/track.h"
#include "svc/msf.h"

namespace mister::svc::disc {

struct QTimes {
    Msf relative{};
    Msf absolute{};
};

struct QChannel {
    TASTY_SEAT_EXEMPT(component);
    static constexpr std::size_t kBytes = 10;

    std::array<std::byte, kBytes> v{};

    [[nodiscard]] static constexpr QChannel of_track(
        QControl control,
        TrackNumber number,
        QTimes times
    ) noexcept {
        QChannel out;
        out.write(kControlAt, control.q_byte());
        out.write(kTnoAt, std::byte{number.bcd_byte()});
        out.write(kIndexAt, kIndex01);
        out.write_times(times);
        return out;
    }

    [[nodiscard]] static constexpr QChannel lead_in(
        QControl control,
        TocPoint point,
        QTimes times
    ) noexcept {
        QChannel out;
        out.write(kControlAt, control.q_byte());
        out.write(kTnoAt, std::byte{});
        out.write(kIndexAt, point.wire_byte());
        out.write_times(times);
        return out;
    }

    constexpr void store(std::span<std::byte, kBytes> dst) const noexcept {
        std::ranges::copy(v, dst.begin());
    }

    [[nodiscard]] constexpr QChannel with_relative(Msf at) const noexcept {
        QChannel out = *this;
        out.write(kMinuteAt, std::byte{at.m});
        out.write(kSecondAt, std::byte{at.s});
        out.write(kFrameAt, std::byte{at.f});
        return out;
    }

    friend constexpr bool operator==(QChannel, QChannel) = default;

private:
    static constexpr infra::ByteOffset kControlAt{0};
    static constexpr infra::ByteOffset kTnoAt{1};
    static constexpr infra::ByteOffset kIndexAt{2};
    static constexpr infra::ByteOffset kMinuteAt{3};
    static constexpr infra::ByteOffset kSecondAt{4};
    static constexpr infra::ByteOffset kFrameAt{5};
    static constexpr infra::ByteOffset kZeroAt{6};
    static constexpr infra::ByteOffset kAbsMinuteAt{7};
    static constexpr infra::ByteOffset kAbsSecondAt{8};
    static constexpr infra::ByteOffset kAbsFrameAt{9};
    static constexpr std::byte kIndex01{proto::PackedBcd::unmasked_byte(1)};

    constexpr void write(infra::ByteOffset at, std::byte byte) noexcept {
        infra::affine::from(std::span{v}, at)[0] = byte;
    }

    constexpr void write_times(QTimes times) noexcept {
        write(kMinuteAt, std::byte{times.relative.m});
        write(kSecondAt, std::byte{times.relative.s});
        write(kFrameAt, std::byte{times.relative.f});
        write(kZeroAt, std::byte{});
        write(kAbsMinuteAt, std::byte{times.absolute.m});
        write(kAbsSecondAt, std::byte{times.absolute.s});
        write(kAbsFrameAt, std::byte{times.absolute.f});
    }
};

}
