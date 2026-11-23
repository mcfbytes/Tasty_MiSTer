// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/reg_field.h"
#include "infra/affine.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"
#include "proto/types.h"
#include "svc/disc/track.h"
#include "svc/msf.h"
#include "svc/types.h"

namespace mister::cores::minimig::cr511 {

enum class AddressForm : std::uint8_t { Lba, Msf };

struct Address {
    std::uint32_t v = 0;

    struct Pair;

    static constexpr hal::RegField<std::uint32_t> kHighByte{16, 0xFF};
    static constexpr hal::RegField<std::uint32_t> kMidByte{8, 0xFF};
    static constexpr hal::RegField<std::uint32_t> kLowByte{0, 0xFF};

    [[nodiscard]] static constexpr Address load(std::span<const std::byte, 3> be24) noexcept {
        constexpr std::size_t kHighAt = 0;
        constexpr std::size_t kMidAt = 1;
        constexpr std::size_t kLowAt = 2;
        return Address{
            kHighByte.set(0, {std::to_integer<std::uint8_t>(be24[kHighAt])})
            | kMidByte.set(0, {std::to_integer<std::uint8_t>(be24[kMidAt])})
            | kLowByte.set(0, {std::to_integer<std::uint8_t>(be24[kLowAt])})
        };
    }

    [[nodiscard]] static constexpr Address of_disc(proto::Lba at, AddressForm form) noexcept {
        return form == AddressForm::Msf ? of_msf(at + svc::disc::kRedBookPregap)
                                        : Address{(at - proto::Lba{}) / proto::BlockCount{1}};
    }

    [[nodiscard]] static constexpr Address of_count(
        proto::BlockCount n,
        AddressForm form
    ) noexcept {
        return form == AddressForm::Msf ? of_msf(proto::Lba{} + n)
                                        : Address{n / proto::BlockCount{1}};
    }

    [[nodiscard]] static constexpr Address of_msf(proto::Lba at) noexcept {
        const std::uint32_t f = (at - proto::Lba{}) / proto::BlockCount{1};
        return Address{
            kHighByte.set(0, {static_cast<std::uint8_t>(f / svc::kFramesPerMinute)})
            | kMidByte.set(
                0,
                {static_cast<std::uint8_t>((f / svc::kFramesPerSecond) % svc::kSecondsPerMinute)}
            )
            | kLowByte.set(0, {static_cast<std::uint8_t>(f % svc::kFramesPerSecond)})
        };
    }

    [[nodiscard]] static constexpr Address of_a0(
        svc::TrackNumber first,
        AddressForm form
    ) noexcept {
        return of_disc(proto::Lba{first.v}, form);
    }

    [[nodiscard]] static constexpr Address of_a1(svc::TrackCount real, AddressForm form) noexcept {
        return of_disc(proto::Lba{real.v}, form);
    }

    constexpr void store(std::span<std::byte, 3> be24) const noexcept {
        constexpr std::size_t kHighAt = 0;
        constexpr std::size_t kMidAt = 1;
        constexpr std::size_t kLowAt = 2;
        be24[kHighAt] = std::byte{static_cast<std::uint8_t>(kHighByte.get(v))};
        be24[kMidAt] = std::byte{static_cast<std::uint8_t>(kMidByte.get(v))};
        be24[kLowAt] = std::byte{static_cast<std::uint8_t>(kLowByte.get(v))};
    }

    [[nodiscard]] constexpr proto::Lba lba() const noexcept { return proto::Lba{v}; }

    [[nodiscard]] constexpr proto::Lba msf_lba() const noexcept {
        const std::uint32_t minute = kHighByte.get(v);
        const std::uint32_t second = kMidByte.get(v);
        const std::uint32_t frame = kLowByte.get(v);
        const proto::Lba lsn{
            (minute * svc::kSecondsPerMinute + second) * svc::kFramesPerSecond + frame
        };
        return lsn >= proto::Lba{} + svc::disc::kRedBookPregap ? lsn - svc::disc::kRedBookPregap
                                                               : proto::Lba{};
    }

    [[nodiscard]] constexpr proto::BlockCount count() const noexcept {
        return proto::BlockCount{v};
    }

    [[nodiscard]] constexpr bool open_end() const noexcept;

    friend constexpr bool operator==(Address, Address) = default;
};

struct Address::Pair {
    Address first;
    Address second;

    [[nodiscard]] static constexpr Pair load(std::span<const std::byte, 6> two) noexcept {
        return Pair{
            .first = Address::load(two.first<3>()),
            .second = Address::load(two.subspan<3, 3>())
        };
    }

    constexpr void store(std::span<std::byte, 6> two) const noexcept {
        first.store(two.first<3>());
        second.store(two.subspan<3, 3>());
    }
};

inline constexpr Address kOpenEnd{0xFFFFFFu};

inline constexpr proto::Lba kNoEnd{0xFFFFFFFFu};

constexpr bool Address::open_end() const noexcept {
    return *this == kOpenEnd;
}

}
