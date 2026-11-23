// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/atari8/atari8_status2.h"
#include "cores/atari8/pbi_drive_mode.h"
#include "cores/atari8/sio_call.h"
#include "cores/atari8/sio_types.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/seat.h"
#include "infra/span_cut.h"

namespace mister::cores::atari8 {

struct DcbLength {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr DcbLength load_le(std::span<const std::byte, 2> b) noexcept {
        return DcbLength{infra::load_le<std::uint16_t>(b)};
    }

    [[nodiscard]] static constexpr DcbLength of(infra::ByteCount bytes) noexcept {
        return DcbLength{static_cast<std::uint16_t>(bytes.v)};
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    [[nodiscard]] constexpr std::array<std::byte, 2> le_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_le(std::span<std::byte, 2>{out}, v);
        return out;
    }

    friend constexpr bool operator==(DcbLength, DcbLength) = default;
    friend constexpr auto operator<=>(DcbLength, DcbLength) = default;
};

struct HostRun {
    HostAddress at;
    std::span<const std::byte> bytes;
};

struct PbiMailboxBlock {
    TASTY_SEAT_EXEMPT(component);

    static constexpr infra::ByteCount kBytes{16};
    std::array<std::byte, kBytes.size()> v{};

    static constexpr infra::ByteOffset kMarkAt{0x00};
    static constexpr infra::ByteOffset kMarkEchoAt{0x01};
    static constexpr infra::ByteOffset kSplashAt{0x02};
    static constexpr infra::ByteOffset kInitAskedAt{0x03};
    static constexpr infra::ByteOffset kModeAt{0x04};
    static constexpr infra::ByteOffset kRequestAskedAt{0x05};
    static constexpr infra::ByteOffset kBootAt{0x0A};
    static constexpr infra::ByteOffset kBootDriveAt{0x0B};
    static constexpr infra::ByteOffset kConfigsAt{0x0C};
    static constexpr infra::ByteCount kConfigBytes{6};
    static constexpr infra::ByteCount kFlagBytes{2};

    static constexpr HostAddress kBase{0xD100};
    static constexpr std::byte kMark{0xA5};
    static constexpr std::byte kAsked{0x01};
    static constexpr std::byte kCleared{0x00};
    static constexpr std::byte kFlagOn{0x01};

    [[nodiscard]] constexpr std::span<std::byte> bytes() noexcept { return v; }

    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool marked() const noexcept {
        return infra::affine::from(bytes(), kMarkAt).front() == kMark
            && infra::affine::from(bytes(), kMarkEchoAt).front() == kMark;
    }

    [[nodiscard]] constexpr bool init_asked() const noexcept {
        return infra::affine::from(bytes(), kInitAskedAt).front() == kAsked;
    }

    [[nodiscard]] constexpr bool request_asked() const noexcept {
        return infra::affine::from(bytes(), kRequestAskedAt).front() == kAsked;
    }

    constexpr void configure(Atari8Status2 modes, std::byte boot_partition_or_drive) noexcept {
        std::ranges::copy(modes.config_bytes(), infra::affine::from(bytes(), kConfigsAt).begin());
        infra::affine::from(bytes(), kBootDriveAt).front() = modes.boot_drive().wire_byte();
        infra::affine::from(bytes(), kBootAt).front() = boot_partition_or_drive;
    }

    constexpr void take_init(bool splash) noexcept {
        infra::affine::from(bytes(), kSplashAt).front() = splash ? kFlagOn : kCleared;
        infra::affine::from(bytes(), kInitAskedAt).front() = kCleared;
    }

    constexpr void answer(PbiDriveMode mode) noexcept {
        infra::affine::from(bytes(), kModeAt).front() = answer_byte(mode);
        infra::affine::from(bytes(), kRequestAskedAt).front() = kCleared;
    }

    [[nodiscard]] constexpr HostRun config_run() const noexcept {
        const std::span<const std::byte> block = bytes();
        return HostRun{
            .at = kBase.at(kBootAt),
            .bytes = infra::affine::first(infra::affine::from(block, kBootAt), kConfigBytes)
        };
    }

    [[nodiscard]] constexpr HostRun flag_run() const noexcept {
        const std::span<const std::byte> block = bytes();
        return HostRun{
            .at = kBase.at(kSplashAt),
            .bytes = infra::affine::first(infra::affine::from(block, kSplashAt), kFlagBytes)
        };
    }

    [[nodiscard]] constexpr HostRun answer_run() const noexcept {
        const std::span<const std::byte> block = bytes();
        return HostRun{
            .at = kBase.at(kModeAt),
            .bytes = infra::affine::first(infra::affine::from(block, kModeAt), kFlagBytes)
        };
    }

    friend constexpr bool operator==(PbiMailboxBlock, PbiMailboxBlock) = default;
};

struct HostDcb {
    TASTY_SEAT_EXEMPT(component);

    static constexpr infra::ByteCount kBytes{16};
    std::array<std::byte, kBytes.size()> v{};

    static constexpr infra::ByteOffset kDeviceAt{0x00};
    static constexpr infra::ByteOffset kUnitAt{0x01};
    static constexpr infra::ByteOffset kCommandAt{0x02};
    static constexpr infra::ByteOffset kStatusAt{0x03};
    static constexpr infra::ByteOffset kBufferAt{0x04};
    static constexpr infra::ByteOffset kLengthAt{0x08};
    static constexpr infra::ByteOffset kAuxAt{0x0A};

    static constexpr HostAddress kBase{0x0300};

    [[nodiscard]] constexpr std::span<std::byte> bytes() noexcept { return v; }

    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept { return v; }

    [[nodiscard]] constexpr std::byte device() const noexcept {
        return infra::affine::from(bytes(), kDeviceAt).front();
    }

    [[nodiscard]] constexpr std::byte unit() const noexcept {
        return infra::affine::from(bytes(), kUnitAt).front();
    }

    [[nodiscard]] constexpr std::byte command() const noexcept {
        return infra::affine::from(bytes(), kCommandAt).front();
    }

    [[nodiscard]] constexpr SioDirection direction() const noexcept {
        return direction_of(infra::affine::from(bytes(), kStatusAt).front());
    }

    [[nodiscard]] constexpr HostAddress buffer() const noexcept {
        return HostAddress::load_le(infra::affine::from(bytes(), kBufferAt).first<2>());
    }

    [[nodiscard]] constexpr DcbLength length() const noexcept {
        return DcbLength::load_le(infra::affine::from(bytes(), kLengthAt).first<2>());
    }

    [[nodiscard]] constexpr SioSector sector(bool long_form) const noexcept {
        const std::span<const std::byte> aux = infra::affine::from(bytes(), kAuxAt);
        return long_form ? SioSector::load_le(aux.first<4>()) : SioSector::load_le(aux.first<2>());
    }

    constexpr void set_status(std::byte status) noexcept {
        infra::affine::from(bytes(), kStatusAt).front() = status;
    }

    constexpr void set_length(DcbLength length) noexcept {
        std::ranges::copy(length.le_bytes(), infra::affine::from(bytes(), kLengthAt).begin());
    }

    [[nodiscard]] constexpr HostRun length_run() const noexcept {
        const std::span<const std::byte> block = bytes();
        return HostRun{
            .at = kBase.at(kLengthAt),
            .bytes = infra::affine::from(block, kLengthAt).first<2>()
        };
    }

    [[nodiscard]] constexpr HostRun status_run() const noexcept {
        const std::span<const std::byte> block = bytes();
        return HostRun{
            .at = kBase.at(kStatusAt),
            .bytes = infra::affine::from(block, kStatusAt).first<1>()
        };
    }

    friend constexpr bool operator==(HostDcb, HostDcb) = default;
};

}
