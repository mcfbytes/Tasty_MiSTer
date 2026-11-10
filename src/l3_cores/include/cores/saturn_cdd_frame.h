// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/types.h"
#include "infra/seat.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::cores {

struct SaturnCddFrame {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kBytes = 12;
    static constexpr std::size_t kSumBytes = 11;
    static constexpr std::size_t kChecksumAt = kSumBytes;

    enum class Field : std::uint8_t {
        Status = 0,
        Control = 1,
        Track = 2,
        Index = 3,
        Minute = 4,
        Second = 5,
        Frame = 6,
        Zero = 7,
        AbsMinute = 8,
        AbsSecond = 9,
        AbsFrame = 10,
        Checksum = 11,
    };

    enum class CommField : std::uint8_t {
        Command = 0,
        FadHigh = 1,
        FadMid = 2,
        FadLow = 3,
        Index = 4,
        Track = 8,
        Speed = 10,
    };

    struct Comm {
        std::uint8_t command = 0;
        HeadLba head{};
        std::uint8_t index = 0;
        svc::TrackNumber track{};
        std::uint8_t speed = 0;
        friend constexpr bool operator==(Comm, Comm) = default;
    };

    static constexpr std::size_t kFadBytes = 3;

    static constexpr SectorCount kPregap = SectorCount::of(svc::kRedBookPregap);

    [[nodiscard]] static constexpr std::array<std::uint8_t, kFadBytes> fad_bytes(
        HeadLba head
    ) noexcept {
        const auto fad = static_cast<std::uint32_t>((head + kPregap).v);
        return {
            static_cast<std::uint8_t>(fad >> 16),
            static_cast<std::uint8_t>(fad >> 8),
            static_cast<std::uint8_t>(fad)
        };
    }

    struct Checksum {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr std::uint8_t byte() const noexcept { return v; }

        [[nodiscard]] constexpr bool matches(std::uint8_t byte) const noexcept { return v == byte; }

        constexpr void store(std::span<std::uint8_t, 1> dst) const noexcept { dst[0] = byte(); }

        friend constexpr bool operator==(Checksum, Checksum) = default;
    };

    [[nodiscard]] static constexpr Checksum checksum(
        std::span<const std::uint8_t, kSumBytes> payload
    ) noexcept {
        std::uint8_t sum = 0;
        for (const std::uint8_t b : payload)
            sum = static_cast<std::uint8_t>(sum + b);
        return Checksum{static_cast<std::uint8_t>(~sum)};
    }

    std::array<std::uint8_t, kBytes> bytes{};

    [[nodiscard]] constexpr Checksum checksum() const noexcept {
        return checksum(std::span<const std::uint8_t>(bytes).first<kSumBytes>());
    }

    [[nodiscard]] constexpr bool checksum_ok() const noexcept {
        return checksum().matches(bytes[kChecksumAt]);
    }

    constexpr void seal() noexcept {
        checksum().store(std::span<std::uint8_t, 1>(bytes.data() + kChecksumAt, 1));
    }

    constexpr void set(Field f, std::uint8_t value) noexcept {
        bytes[static_cast<std::size_t>(f)] = value;
    }

    [[nodiscard]] constexpr std::uint8_t at(Field f) const noexcept {
        return bytes[static_cast<std::size_t>(f)];
    }

    constexpr void load_words(std::span<const std::uint16_t> words) noexcept {
        for (std::size_t i = 0; i < words.size() && 2 * i + 1 < kBytes; ++i) {
            bytes[2 * i] = static_cast<std::uint8_t>(words[i]);
            bytes[2 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
        }
    }

    [[nodiscard]] constexpr std::uint16_t word(std::size_t i) const noexcept {
        return static_cast<std::uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    }

    [[nodiscard]] constexpr Comm comm() const noexcept {
        const auto b = [this](CommField f) {
            return bytes[static_cast<std::size_t>(f)];
        };
        constexpr unsigned kByteBits = 8;
        const std::uint32_t fad = (std::uint32_t{b(CommField::FadHigh)} << (2 * kByteBits))
            | (std::uint32_t{b(CommField::FadMid)} << kByteBits)
            | std::uint32_t{b(CommField::FadLow)};
        Comm out{};
        out.command = b(CommField::Command);
        out.head = HeadLba::of(proto::Lba{fad}) - kPregap;
        out.index = b(CommField::Index);
        out.track = svc::TrackNumber{b(CommField::Track)};
        out.speed = b(CommField::Speed);
        return out;
    }

    friend constexpr bool operator==(SaturnCddFrame, SaturnCddFrame) = default;
};

static_assert(sizeof(SaturnCddFrame) == SaturnCddFrame::kBytes);
static_assert(SaturnCddFrame::kChecksumAt + 1 == SaturnCddFrame::kBytes);

}
