// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/ide_identify.h"

#include <algorithm>
#include <array>
#include <span>

#include "hal/spi_halves.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

namespace {

using Word = IdeIdentify::Word;

struct FixedWords {
    IdeIdentify::Words run;
    hal::SpiWord value;
};

constexpr hal::SpiWord kBlank{0x2020};

constexpr std::array kFixedWords{
    FixedWords{{Word::Configuration, Word::Configuration}, hal::SpiWord{0x0040}},
    FixedWords{{Word::SectorBytes, Word::SectorBytes}, hal::SpiWord{512}},
    FixedWords{{Word::SerialFirst, Word::SerialLast}, kBlank},
    FixedWords{{Word::BufferType, Word::BufferType}, hal::SpiWord{3}},
    FixedWords{{Word::CacheBytes, Word::CacheBytes}, hal::SpiWord{512}},
    FixedWords{{Word::EccBytes, Word::EccBytes}, hal::SpiWord{4}},
    FixedWords{{Word::ModelFirst, Word::ModelLast}, kBlank},
    FixedWords{{Word::MostMultiple, Word::MostMultiple}, hal::SpiWord{0x8020}},
    FixedWords{{Word::DwordIo, Word::DwordIo}, hal::SpiWord{1}},
    FixedWords{{Word::Capabilities, Word::Capabilities}, hal::SpiWord{0x0200}},
    FixedWords{{Word::StandbyTimer, Word::StandbyTimer}, hal::SpiWord{0x4001}},
    FixedWords{{Word::PioTiming, Word::PioTiming}, hal::SpiWord{0x0200}},
    FixedWords{{Word::DmaTiming, Word::DmaTiming}, hal::SpiWord{0x0200}},
    FixedWords{{Word::ValidFields, Word::ValidFields}, hal::SpiWord{0x0007}},
    FixedWords{{Word::Multiple, Word::Multiple}, hal::SpiWord{0x0110}},
    FixedWords{{Word::CycleFirst, Word::CycleLast}, hal::SpiWord{120}},
    FixedWords{{Word::AtaVersions, Word::AtaVersions}, hal::SpiWord{0x007E}},
    FixedWords{{Word::Supported, Word::Supported}, hal::SpiWord{0x4200}},
    FixedWords{{Word::SupportedMore, Word::SupportedMore}, hal::SpiWord{0x7000}},
    FixedWords{{Word::SupportedExtension, Word::SupportedExtension}, hal::SpiWord{0x4000}},
    FixedWords{{Word::Enabled, Word::Enabled}, hal::SpiWord{0x4200}},
    FixedWords{{Word::EnabledMore, Word::EnabledMore}, hal::SpiWord{0x7000}},
    FixedWords{{Word::EnabledDefault, Word::EnabledDefault}, hal::SpiWord{0x4000}},
    FixedWords{{Word::HardwareReset, Word::HardwareReset}, hal::SpiWord{0x630B}},
};

constexpr std::string_view kSerial = "AOHD00000 ";

[[nodiscard]] hal::SpiHalves total_words(proto::BlockCount total) noexcept {
    return hal::SpiHalves::of(total / proto::BlockCount{1});
}

void lay_text(std::span<std::byte> field, std::string_view text) noexcept {
    const std::span<const std::byte> bytes = std::as_bytes(std::span{text});
    for (std::size_t i = 0; i < bytes.size() && i < field.size(); ++i)
        field[i ^ 1u] = bytes[i];
}

}

IdeIdentify IdeIdentify::of(const IdeMedia& media, std::string_view name) noexcept {
    const svc::DiskGeometry& shape = media.geometry;
    const hal::SpiHalves total = total_words(media.total);
    IdeIdentify id{};
    for (const FixedWords& fixed : kFixedWords)
        id.fill(fixed.run, fixed.value);
    lay_text(id.bytes_of(Words{Word::SerialFirst, Word::SerialLast}), kSerial);
    lay_text(id.bytes_of(Words{Word::ModelFirst, Word::ModelLast}), name);

    id.put(Word::Cylinders, shape.cylinders.word());
    id.put(Word::Heads, shape.heads.word());
    id.put(
        Word::TrackBytes,
        hal::SpiHalves::of(svc::DiskGeometry::kSector.window_bytes(shape.sectors.track_blocks()))
            .low
    );
    id.put(Word::Sectors, shape.sectors.word());
    id.put(Word::CurrentCylinders, shape.cylinders.word());
    id.put(Word::CurrentHeads, shape.heads.word());
    id.put(Word::CurrentSectors, shape.sectors.word());
    id.put_pair(Word::CurrentTotal, total);
    id.put_pair(Word::LbaTotal, total);
    id.put_pair(Word::Lba48Total, total);
    return id;
}

}
