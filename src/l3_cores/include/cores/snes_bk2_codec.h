// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_archive.h"
#include "cores/movie_codec.h"
#include "cores/snes_movie_codec.h"
#include "cores/types.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class SnesBk2Codec final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Button = SnesMovieCodec::Button;

    static constexpr std::uint8_t kColReset = 1;
    static constexpr std::uint8_t kColPower = 2;
    static constexpr std::uint8_t kColSubframe = 3;
    static constexpr std::uint8_t kColResetDelay = 4;
    static constexpr std::uint8_t kColPad = 0x80;
    static constexpr std::uint8_t kPortShift = 4;
    static constexpr std::uint8_t kPortMask = 0x7;
    static constexpr std::uint8_t kButtonMask = 0xF;
    static constexpr std::uint8_t kColGroupEnd = 0xFF;

    static constexpr Bk2Layout kMovieVersion = Bk2Layout::MovieVersion;
    static constexpr Bk2Layout kPlatform = Bk2Layout::Platform;
    static constexpr Bk2Layout kLogKey = Bk2Layout::LogKey;
    static constexpr Bk2Layout kSha1 = Bk2Layout::Sha1;

    static constexpr std::string_view kPlatformName = "SNES";

    static constexpr Bk2Layout kBsnes115 = Bk2Layout::Bsnes115;

    static constexpr Bk2Layout kLibsnes = Bk2Layout::Libsnes;
    static constexpr Bk2Layout kLibsnesFlat = Bk2Layout::LibsnesFlat;

    static constexpr std::uint32_t kSandboxClockSeed = static_cast<std::uint32_t>(
        1495889068ull * 1000000ull
    );

    static constexpr ReplayLead kBizHawkLead{-1};
    static constexpr ReplayLead kLibsnesLead{-2};

    [[nodiscard]] infra::OptRef<const MovieArchiveFormat> archive() const noexcept override {
        return infra::OptRef<const MovieArchiveFormat>{kBk2Archive};
    }

    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<MovieFacts> header_line(
        std::string_view line,
        const MovieFacts& so_far
    ) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Ex<MovieFrame> frame(
        std::string_view line,
        const MovieFacts& f
    ) const noexcept override;

    [[nodiscard]] ReplayLead default_lead(const MovieFacts& f) const noexcept override {
        return f.layout.has(Bk2Layout::Libsnes) ? kLibsnesLead : kBizHawkLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept override;

    struct PadName {
        std::string_view name;
        Button button;
    };

    static constexpr std::array<PadName, 12> kPadNames{
        {{"Up", Button::Up},
         {"Down", Button::Down},
         {"Left", Button::Left},
         {"Right", Button::Right},
         {"Select", Button::Select},
         {"Start", Button::Start},
         {"Y", Button::Y},
         {"B", Button::B},
         {"X", Button::X},
         {"A", Button::A},
         {"L", Button::L},
         {"R", Button::R}}
    };

    struct Bk2Column {
        std::uint8_t v = 0;

        [[nodiscard]] static constexpr Bk2Column pad(
            svc::PlayerIndex port,
            SnesMovieCodec::Button button
        ) noexcept {
            return Bk2Column{static_cast<std::uint8_t>(
                static_cast<unsigned>(kColPad)
                | (static_cast<unsigned>(port.v) << kPortShift)
                | static_cast<unsigned>(button)
            )};
        }

        [[nodiscard]] constexpr bool is_pad() const noexcept { return (v & kColPad) != 0; }

        [[nodiscard]] constexpr bool is_group_end() const noexcept { return v == kColGroupEnd; }

        [[nodiscard]] constexpr bool is_reset_delay() const noexcept { return v == kColResetDelay; }

        [[nodiscard]] constexpr bool is_reset() const noexcept { return v == kColReset; }

        [[nodiscard]] constexpr bool is_power() const noexcept { return v == kColPower; }

        [[nodiscard]] constexpr bool is_subframe() const noexcept { return v == kColSubframe; }

        [[nodiscard]] constexpr svc::PlayerIndex port() const noexcept {
            return svc::PlayerIndex::of(static_cast<unsigned>((v >> kPortShift) & kPortMask));
        }

        [[nodiscard]] constexpr JoyBit bit() const noexcept {
            return JoyBit{static_cast<std::uint8_t>(v & kButtonMask)};
        }

        [[nodiscard]] constexpr bool append_to(MovieFacts& f) const noexcept {
            const std::span<std::uint8_t> slots = f.columns_n.slots_from(f.columns);
            if (slots.empty())
                return false;
            slots[0] = v;
            f.columns_n = f.columns_n.next();
            return true;
        }

        friend constexpr bool operator==(Bk2Column, Bk2Column) = default;
    };

    [[nodiscard]] static Ex<Bk2Column> column_of(std::string_view name) noexcept;
};

consteval std::array<SnesMovieCodec::Button, 12> bk2_pad_order() {
    std::array<SnesMovieCodec::Button, 12> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = SnesBk2Codec::kPadNames[i].button;
    return out;
}

static_assert(
    snes_pad_order_is_a_permutation(bk2_pad_order()),
    "BizHawk's SNES pad names are a permutation of the core's twelve buttons"
);

}
