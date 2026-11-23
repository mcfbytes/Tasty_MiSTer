// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include "cores/ide/ide_media.h"
#include "hal/spi_halves.h"
#include "hal/types.h"
#include "infra/seat.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

class IdeIdentify {
    TASTY_SEAT_EXEMPT(component);

public:
    using Image = std::array<std::byte, svc::DiskGeometry::kSector.block_bytes()>;

    enum class Word : std::size_t {
        Configuration = 0,
        Cylinders = 1,
        Heads = 3,
        TrackBytes = 4,
        SectorBytes = 5,
        Sectors = 6,
        SerialFirst = 10,
        SerialLast = 19,
        BufferType = 20,
        CacheBytes = 21,
        EccBytes = 22,
        ModelFirst = 27,
        ModelLast = 46,
        MostMultiple = 47,
        DwordIo = 48,
        Capabilities = 49,
        StandbyTimer = 50,
        PioTiming = 51,
        DmaTiming = 52,
        ValidFields = 53,
        CurrentCylinders = 54,
        CurrentHeads = 55,
        CurrentSectors = 56,
        CurrentTotal = 57,
        Multiple = 59,
        LbaTotal = 60,
        CycleFirst = 65,
        CycleLast = 68,
        AtaVersions = 80,
        Supported = 82,
        SupportedMore = 83,
        SupportedExtension = 84,
        Enabled = 85,
        EnabledMore = 86,
        EnabledDefault = 87,
        HardwareReset = 93,
        Lba48Total = 100,
    };

    struct Words {
        Word first;
        Word last;
    };

    constexpr IdeIdentify() noexcept = default;

    [[nodiscard]] static IdeIdentify of(const IdeMedia& media, std::string_view name) noexcept;

    [[nodiscard]] constexpr std::span<const std::byte, std::tuple_size_v<Image>>
    bytes() const noexcept {
        return image_;
    }

    [[nodiscard]] constexpr hal::SpiWord word(Word at) const noexcept {
        return hal::SpiWord::load_le(
            std::span<const std::byte>(image_).subspan(byte_of(at)).first<sizeof(hal::SpiWord)>()
        );
    }

    friend constexpr bool operator==(const IdeIdentify&, const IdeIdentify&) = default;

private:
    [[nodiscard]] static constexpr std::size_t byte_of(Word at) noexcept {
        return std::to_underlying(at) * sizeof(hal::SpiWord);
    }

    [[nodiscard]] constexpr std::span<std::byte> bytes_of(Words run) noexcept {
        return std::span<std::byte>(image_).subspan(
            byte_of(run.first),
            byte_of(run.last) + sizeof(hal::SpiWord) - byte_of(run.first)
        );
    }

    constexpr void put(Word at, hal::SpiWord value) noexcept {
        value.store_le(bytes_of(Words{at, at}).first<sizeof(hal::SpiWord)>());
    }

    constexpr void put_pair(Word at, hal::SpiHalves value) noexcept {
        const std::span<std::byte> field = std::span<std::byte>(image_).subspan(byte_of(at));
        value.low.store_le(field.first<sizeof(hal::SpiWord)>());
        value.high.store_le(field.subspan(sizeof(hal::SpiWord)).first<sizeof(hal::SpiWord)>());
    }

    constexpr void fill(Words run, hal::SpiWord value) noexcept {
        std::span<std::byte> rest = bytes_of(run);
        while (!rest.empty()) {
            value.store_le(rest.first<sizeof(hal::SpiWord)>());
            rest = rest.subspan(sizeof(hal::SpiWord));
        }
    }

    Image image_{};
};

}
