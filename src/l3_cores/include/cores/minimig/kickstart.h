// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "cores/minimig/minimig_ladder_bus_addr.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::minimig {

struct Kickstart {
    TASTY_SEAT_EXEMPT(component);

    enum class Form : std::uint8_t {
        Rom1M,
        BootRomKeyed,
        BootRom,
        Rom512K,
        Rom512KKeyed,
        Rom256K,
        Rom256KKeyed,
        WithExtRom,
        kCount,
    };

    struct Region {
        BusAddr at{};
        std::vector<std::byte> bytes;
    };

    struct Key {
        std::span<const std::byte> bytes{};

        [[nodiscard]] static Key usable(std::span<const std::byte> file) noexcept;

        [[nodiscard]] bool present() const noexcept { return !bytes.empty(); }
    };

    struct RomPair {
        std::span<const std::byte> kick{};
        std::span<const std::byte> ext{};
    };

    static constexpr proto::FileSize kRom256K{0x40000};
    static constexpr proto::FileSize kRom512K{0x80000};
    static constexpr proto::FileSize kRom1M{0x100000};
    static constexpr proto::FileSize kBootRom{0x2000};

    static constexpr proto::FileSize kKeyedHeader{0xB};

    static constexpr proto::FileSize kLargestFile = kRom1M;
    static constexpr proto::FileSize kReadCap = kLargestFile + kKeyedHeader + proto::FileSize{1};

    static constexpr proto::FileSize kKeyBuffer{3072};

    [[nodiscard]] static std::optional<Form> form_of(proto::FileSize size, bool keyed) noexcept;

    [[nodiscard]] static std::optional<Kickstart> route(std::span<const std::byte> file, Key key);

    [[nodiscard]] static std::optional<Kickstart> with_ext_rom(RomPair roms);

    Form form = Form::kCount;
    std::vector<Region> regions{};

    bool clears_tag = false;
};

}
