// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include "cores/digest_value.h"
#include "cores/movie_region.h"
#include "cores/movie_rerecord_count.h"
#include "cores/ram_image_recipe.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

inline constexpr std::size_t kMovieColumnsMax = 48;

struct MovieRevision {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr std::uint16_t number() const noexcept { return v; }

    [[nodiscard]] static constexpr MovieRevision of_char(char c) noexcept {
        return MovieRevision{static_cast<std::uint16_t>(static_cast<unsigned char>(c))};
    }

    [[nodiscard]] constexpr bool unset() const noexcept { return v == 0; }

    friend constexpr bool operator==(MovieRevision, MovieRevision) = default;
    friend constexpr auto operator<=>(MovieRevision, MovieRevision) = default;
};

enum class Bk2Layout : std::uint16_t {
    MovieVersion = 0x01,
    Platform = 0x02,
    LogKey = 0x04,
    Sha1 = 0x08,
    Bsnes115 = 0x10,
    EntropyBit0 = 0x20,
    EntropyBit1 = 0x40,
    Libsnes = 0x80,
    LibsnesFlat = 0x100,
};

struct FormatLayout {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr FormatLayout of(std::uint16_t bits) noexcept {
        return FormatLayout{bits};
    }

    [[nodiscard]] constexpr std::uint16_t bits() const noexcept { return v; }

    template <class Flag>
    [[nodiscard]] constexpr bool has(Flag flag) const noexcept {
        return (v & bits_of(flag)) != 0;
    }

    template <class... Flags>
    [[nodiscard]] constexpr bool has_all(Flags... flags) const noexcept {
        return (has(flags) && ...);
    }

    template <class Flag>
    [[nodiscard]] constexpr FormatLayout with(Flag flag) const noexcept {
        return FormatLayout{static_cast<std::uint16_t>(v | bits_of(flag))};
    }

    template <class Flag>
    [[nodiscard]] constexpr FormatLayout with(Flag flag, bool on) const noexcept {
        return on ? with(flag) : without(flag);
    }

    template <class Flag>
    [[nodiscard]] constexpr FormatLayout without(Flag flag) const noexcept {
        return FormatLayout{static_cast<std::uint16_t>(v & ~bits_of(flag))};
    }

    template <class Layout>
    [[nodiscard]] constexpr Layout view_as() const noexcept {
        return Layout::of(v);
    }

    friend constexpr bool operator==(FormatLayout, FormatLayout) = default;

private:
    template <class Flag>
    [[nodiscard]] static constexpr std::uint16_t bits_of(Flag flag) noexcept {
        static_assert(std::is_enum_v<Flag>, "a layout flag is a scoped enum");
        return static_cast<std::uint16_t>(std::to_underlying(flag));
    }
};

struct ColumnCount {
    std::uint8_t v = 0;

    struct Indices {
        std::size_t v = 0;

        [[nodiscard]] constexpr auto begin() const noexcept {
            return std::views::iota(std::size_t{0}, v).begin();
        }

        [[nodiscard]] constexpr auto end() const noexcept {
            return std::views::iota(std::size_t{0}, v).end();
        }
    };

    [[nodiscard]] static constexpr ColumnCount of(std::size_t n) noexcept {
        return ColumnCount{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr Indices indices() const noexcept { return Indices{v}; }

    template <std::size_t N>
    [[nodiscard]] constexpr std::span<std::uint8_t> slots_from(
        std::array<std::uint8_t, N>& columns
    ) const noexcept {
        return std::span<std::uint8_t>(columns).subspan(v);
    }

    [[nodiscard]] constexpr bool within(ColumnCount limit) const noexcept { return v < limit.v; }

    [[nodiscard]] constexpr ColumnCount next() const noexcept {
        return ColumnCount{static_cast<std::uint8_t>(v + 1)};
    }

    friend constexpr bool operator==(ColumnCount, ColumnCount) = default;
    friend constexpr auto operator<=>(ColumnCount, ColumnCount) = default;
};

struct MovieFacts {
    MovieRevision version{};
    MovieRegion region = MovieRegion::Ntsc;

    svc::PlayerMask ports{};

    FormatLayout layout{};
    ColumnCount columns_n{};
    std::array<std::uint8_t, kMovieColumnsMax> columns{};

    std::optional<DigestValue> digest{};

    std::optional<DigestValue> firmware{};

    std::optional<MovieRerecordCount> rerecords{};

    RamImageRecipe recorder_ram{};

    std::optional<proto::FieldValue> recorder_ram_fill{};
};

}
