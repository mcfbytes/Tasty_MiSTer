// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/ide_identify.h"

#include <algorithm>
#include <array>
#include <span>

#include "hal/link_halves.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

namespace {

using Word = IdeIdentify::Word;

struct FixedWords {
    IdeIdentify::Words run;
    hal::LinkWord value;
};

constexpr hal::LinkWord kBlank{0x2020};

constexpr std::array kFixedWords{
    FixedWords{{Word::Configuration, Word::Configuration}, hal::LinkWord{0x0040}},
    FixedWords{{Word::SectorBytes, Word::SectorBytes}, hal::LinkWord{512}},
    FixedWords{{Word::SerialFirst, Word::SerialLast}, kBlank},
    FixedWords{{Word::BufferType, Word::BufferType}, hal::LinkWord{3}},
    FixedWords{{Word::CacheBytes, Word::CacheBytes}, hal::LinkWord{512}},
    FixedWords{{Word::EccBytes, Word::EccBytes}, hal::LinkWord{4}},
    FixedWords{{Word::ModelFirst, Word::ModelLast}, kBlank},
    FixedWords{{Word::MostMultiple, Word::MostMultiple}, hal::LinkWord{0x8020}},
    FixedWords{{Word::DwordIo, Word::DwordIo}, hal::LinkWord{1}},
    FixedWords{{Word::Capabilities, Word::Capabilities}, hal::LinkWord{0x0200}},
    FixedWords{{Word::StandbyTimer, Word::StandbyTimer}, hal::LinkWord{0x4001}},
    FixedWords{{Word::PioTiming, Word::PioTiming}, hal::LinkWord{0x0200}},
    FixedWords{{Word::DmaTiming, Word::DmaTiming}, hal::LinkWord{0x0200}},
    FixedWords{{Word::ValidFields, Word::ValidFields}, hal::LinkWord{0x0007}},
    FixedWords{{Word::Multiple, Word::Multiple}, hal::LinkWord{0x0110}},
    FixedWords{{Word::CycleFirst, Word::CycleLast}, hal::LinkWord{120}},
    FixedWords{{Word::AtaVersions, Word::AtaVersions}, hal::LinkWord{0x007E}},
    FixedWords{{Word::Supported, Word::Supported}, hal::LinkWord{0x4200}},
    FixedWords{{Word::SupportedMore, Word::SupportedMore}, hal::LinkWord{0x7000}},
    FixedWords{{Word::SupportedExtension, Word::SupportedExtension}, hal::LinkWord{0x4000}},
    FixedWords{{Word::Enabled, Word::Enabled}, hal::LinkWord{0x4200}},
    FixedWords{{Word::EnabledMore, Word::EnabledMore}, hal::LinkWord{0x7000}},
    FixedWords{{Word::EnabledDefault, Word::EnabledDefault}, hal::LinkWord{0x4000}},
    FixedWords{{Word::HardwareReset, Word::HardwareReset}, hal::LinkWord{0x630B}},
};

constexpr std::string_view kSerial = "AOHD00000 ";

[[nodiscard]] hal::LinkHalves total_words(proto::BlockCount total) noexcept {
    return hal::LinkHalves::of(total / proto::BlockCount{1});
}

void lay_text(std::span<std::byte> field, std::string_view text) noexcept {
    const std::span<const std::byte> bytes = std::as_bytes(std::span{text});
    for (std::size_t i = 0; i < bytes.size() && i < field.size(); ++i)
        field[i ^ 1u] = bytes[i];
}

}

IdeIdentify IdeIdentify::of(const IdeMedia& media, std::string_view name) noexcept {
    const svc::DiskGeometry& shape = media.geometry;
    const hal::LinkHalves total = total_words(media.total);
    IdeIdentify id{};
    for (const FixedWords& fixed : kFixedWords)
        id.fill(fixed.run, fixed.value);
    lay_text(id.bytes_of(Words{Word::SerialFirst, Word::SerialLast}), kSerial);
    lay_text(id.bytes_of(Words{Word::ModelFirst, Word::ModelLast}), name);

    id.put(Word::Cylinders, shape.cylinders.word());
    id.put(Word::Heads, shape.heads.word());
    id.put(
        Word::TrackBytes,
        hal::LinkHalves::of(svc::DiskGeometry::kSector.window_bytes(shape.sectors.track_blocks()))
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
