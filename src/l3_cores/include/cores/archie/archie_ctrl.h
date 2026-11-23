// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/archie/archie_option.h"
#include "infra/endian.h"
#include "proto/bit_field.h"
#include "hal/spi_halves.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores::archie {

struct SystemCtrl {

    struct Home {
        ArchieOption option{};
        proto::BitField file{};
        proto::BitField status{};
    };

    static constexpr std::array<Home, 5> kHomes{{
        {ArchieOption::Aspect, {proto::StatusBit{6}, 2}, {proto::StatusBit{6}, 2}},
        {ArchieOption::Scale, {proto::StatusBit{8}, 2}, {proto::StatusBit{8}, 2}},
        {ArchieOption::Refresh, {proto::StatusBit{4}, 1}, {proto::StatusBit{4}, 1}},
        {ArchieOption::StereoMix, {proto::StatusBit{1}, 2}, {proto::StatusBit{2}, 2}},
        {ArchieOption::AudioFix, {proto::StatusBit{5}, 1}, {proto::StatusBit{5}, 1}},
    }};

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr std::optional<Home> home_of(ArchieOption o) noexcept {
        for (const Home& h : kHomes)
            if (h.option == o)
                return h;
        return std::nullopt;
    }

    [[nodiscard]] constexpr proto::StatusWord image() const noexcept {
        return proto::StatusWord::of(hal::SpiHalves::of(v));
    }

    [[nodiscard]] constexpr proto::FieldValue get(const Home& h) const noexcept {
        return image().field(h.file);
    }

    [[nodiscard]] constexpr SystemCtrl with(const Home& h, proto::FieldValue x) const noexcept {
        const proto::StatusWord next = h.file.write(image(), x);
        return hal::SpiHalves{next.words[0], next.words[1]}.joined([](std::uint32_t word) {
            return SystemCtrl{word};
        });
    }

    [[nodiscard]] static constexpr SystemCtrl load_le(
        std::span<const std::byte, sizeof(std::uint32_t)> b
    ) noexcept {
        return SystemCtrl{infra::load_le<std::uint32_t>(b)};
    }

    constexpr void store_le(std::span<std::byte, sizeof(std::uint32_t)> out) const noexcept {
        infra::store_le<std::uint32_t>(out, v);
    }

    friend constexpr bool operator==(SystemCtrl, SystemCtrl) = default;
};

}
