// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/t64.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"
#include "infra/span_cut.h"
#include "svc/gcr_sector_map.h"
#include "svc/gcr_types.h"

namespace mister::svc::t64 {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

namespace {

inline constexpr std::byte kTapePad{0x20};
inline constexpr std::byte kDiskPad{0xA0};
inline constexpr std::byte kProgram{0x82};

inline constexpr std::byte kFreeEntry{};

enum Tape : std::size_t { kRecordBytes = 32, kNameBytes = 16 };

enum Disk : std::size_t {
    kTracks = 35,
    kSectorBytes = 256,
    kFileSectorBytes = 254,
    kMostFiles = 144,
    kDirEntryBytes = 32,
    kDirEntriesPerSector = 8,
};

inline constexpr gcr::TrackTotal kDiskTracks{kTracks};
inline constexpr proto::BlockCount kFileSectors{683 - 19};
using Name = std::array<std::byte, kNameBytes>;
using Sector = std::array<std::byte, kSectorBytes>;

struct ProgramSize {
    std::uint16_t v = 0;
    using affine_count = ProgramSize;

    static const ProgramSize kLoadAddressBytes;

    [[nodiscard]] static constexpr ProgramSize low_word(infra::ByteCount bytes) noexcept {
        return ProgramSize{static_cast<std::uint16_t>(bytes.v)};
    }

    [[nodiscard]] constexpr ProgramSize with_load_address() const noexcept {
        return *this + kLoadAddressBytes;
    }

    [[nodiscard]] constexpr proto::BlockCount file_sectors() const noexcept {
        return proto::BlockCount{
            static_cast<std::uint32_t>((v + kFileSectorBytes - 1) / kFileSectorBytes)
        };
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    constexpr void store_file_blocks(std::span<std::byte, 2> out) const noexcept {
        (void)file_sectors().store_le16(out);
    }

    friend constexpr auto operator<=>(ProgramSize, ProgramSize) = default;
};

inline constexpr ProgramSize ProgramSize::kLoadAddressBytes{2};

struct LoadAddress {
    std::uint16_t v = 0;
    using affine_count = ProgramSize;

    [[nodiscard]] static constexpr LoadAddress load(std::span<const std::byte, 2> addr) noexcept {
        return LoadAddress{infra::load_le<std::uint16_t>(addr)};
    }

    constexpr void store(std::span<std::byte, 2> addr) const noexcept {
        infra::store_le<std::uint16_t>(addr, v);
    }

    [[nodiscard]] constexpr ProgramSize span_to(LoadAddress end) const noexcept {
        return ProgramSize{static_cast<std::uint16_t>(infra::affine::sub(end.v, v))};
    }

    friend constexpr auto operator<=>(LoadAddress, LoadAddress) = default;
};

struct Record {
    Name name{};
    LoadAddress load_address{};
    ProgramSize size{};
    infra::ByteOffset offset{};
};

struct Place {
    gcr::LogicalTrack track{};
    gcr::SectorNumber sector{};

    static const Place kBam;
    static const Place kFirstDir;
    static const Place kFirstFile;

    [[nodiscard]] constexpr proto::BlockCount sectors_on_track() const noexcept {
        return kMap.sectors_on(full());
    }

    [[nodiscard]] constexpr proto::FileOffset offset() const noexcept {
        return kMap.position_of(full()) + gcr::SectorMap::kSector.bytes_of(sector.before());
    }

    constexpr void advance_file() noexcept {
        const gcr::Interleave step = on_directory_track() ? gcr::Interleave::kDirectory
                                                          : gcr::Interleave::kFile;
        sector = sector.interleaved(step, sectors_on_track());
        if (sector == gcr::SectorNumber{})
            track = track.next_wrapping(kDiskTracks);
    }

    constexpr void advance_dir() noexcept {
        const gcr::Interleave step = on_directory_track() ? gcr::Interleave::kDirectory
                                                          : gcr::Interleave::kFile;
        sector = sector.advanced(step, sectors_on_track());
    }

    [[nodiscard]] constexpr bool on_directory_track() const noexcept { return track == kBam.track; }

    constexpr void step_over_directory() noexcept {
        if (on_directory_track())
            track = track.next();
    }

    [[nodiscard]] constexpr std::byte track_byte() const noexcept { return track.byte(); }

    [[nodiscard]] constexpr std::byte sector_byte() const noexcept { return sector.byte(); }

    friend constexpr bool operator==(Place, Place) = default;

private:
    static constexpr gcr::SectorMap kMap = gcr::SectorMap::of_image(proto::FileSize{});

    [[nodiscard]] constexpr gcr::FullTrack full() const noexcept { return track.on_first_side(); }
};

inline constexpr Place Place::kBam{gcr::LogicalTrack{18}, gcr::SectorNumber{0}};
inline constexpr Place Place::kFirstDir{gcr::LogicalTrack{18}, gcr::SectorNumber{1}};
inline constexpr Place Place::kFirstFile{gcr::LogicalTrack{1}, gcr::SectorNumber{0}};
static_assert(
    Place{gcr::LogicalTrack{18}, gcr::SectorNumber{0}}.offset() == proto::FileOffset{0x16500}
        && Place{gcr::LogicalTrack{35}, gcr::SectorNumber{16}}.offset()
            == proto::FileOffset{0x29A00 + 16 * 256},
    "c64.cpp d64_offset: TrackFileOffset[17] and [34]"
);

struct ChainLink {
    std::array<std::byte, 2> v{};

    static const ChainLink kDirEnd;

    [[nodiscard]] static constexpr ChainLink to(Place next) noexcept {
        return ChainLink{{next.track_byte(), next.sector_byte()}};
    }

    [[nodiscard]] static constexpr ChainLink last(infra::ByteCount used) noexcept {
        return ChainLink{{std::byte{}, static_cast<std::byte>((used + infra::ByteCount{1}).v)}};
    }

    constexpr void store(std::span<std::byte, 2> out) const noexcept {
        std::ranges::copy(v, out.begin());
    }

    friend constexpr bool operator==(ChainLink, ChainLink) = default;
};

inline constexpr ChainLink ChainLink::kDirEnd{{std::byte{}, std::byte{0xFF}}};

struct BamEntry {
    enum : std::size_t { kBytes = 4, kCountBytes = 1 };

    std::array<std::byte, kBytes> v{};

    constexpr void free(gcr::SectorNumber s) noexcept {
        v[0] = static_cast<std::byte>(std::to_integer<unsigned>(v[0]) + 1u);
        infra::at_index(std::span{v}.subspan(kCountBytes), s.free_map_byte()) |= s.free_map_bit();
    }

    constexpr void store(std::span<std::byte, kBytes> out) const noexcept {
        std::ranges::copy(v, out.begin());
    }

    friend constexpr bool operator==(BamEntry, BamEntry) = default;
};

struct Heads {
    Place file{};
    Place dir{};
};

class TapeReader {
public:
    explicit TapeReader(std::span<const std::byte> tape) noexcept : tape_(tape) {}

    void seek(infra::ByteOffset to) noexcept { at_ = to; }

    [[nodiscard]] infra::ByteCount read(std::span<std::byte> dst) noexcept {
        const infra::ByteCount left = end() > at_ ? end() - at_ : infra::ByteCount{};
        const infra::ByteCount n = std::min(infra::ByteCount{dst.size()}, left);
        if (!n.empty()) {
            std::ranges::copy(
                infra::affine::first(infra::affine::from_clamped(tape_, at_), n),
                dst.begin()
            );
        }
        at_ += n;
        return n;
    }

    [[nodiscard]] infra::ByteOffset end() const noexcept { return infra::ByteOffset{tape_.size()}; }

private:
    std::span<const std::byte> tape_;
    infra::ByteOffset at_{};
};

[[nodiscard]] constexpr bool record_used(
    const std::array<std::byte, kRecordBytes>& block
) noexcept {
    return block[0] != kFreeEntry && block[1] != kFreeEntry;
}

struct Catalog {
    Name tape_name{};
    std::vector<Record> files{};
};

[[nodiscard]] std::optional<Catalog> read_catalog(TapeReader& tape) {
    enum Field : std::size_t {
        kRecordCountAt = 0x02,
        kTapeNameAt = 0x08,
        kStartAt = 0x02,
        kEndAt = 0x04,
        kOffsetAt = 0x08,
        kNameAt = 0x10,
    };

    constexpr std::string_view kSignature = "C64";
    std::array<std::byte, kRecordBytes> block{};
    const std::span<const std::byte> view{block};
    if (tape.read(block).empty())
        return std::nullopt;
    if (!std::ranges::equal(view.first(kSignature.size()), std::as_bytes(std::span{kSignature})))
        return std::nullopt;
    if (tape.read(block).empty())
        return std::nullopt;

    Catalog catalog;
    const std::size_t records = infra::load_le<std::uint16_t>(view.subspan<kRecordCountAt, 2>());
    std::ranges::copy(view.subspan<kTapeNameAt, kNameBytes>(), catalog.tape_name.begin());
    for (std::size_t i = 0; i < records; ++i) {
        if (tape.read(block).empty())
            return std::nullopt;
        if (!record_used(block))
            continue;
        Record r;
        const LoadAddress start = LoadAddress::load(view.subspan<kStartAt, 2>());
        const LoadAddress end = LoadAddress::load(view.subspan<kEndAt, 2>());
        r.load_address = start;
        r.size = start.span_to(end);
        r.offset = infra::ByteOffset{infra::load_le<std::uint32_t>(view.subspan<kOffsetAt, 4>())};
        std::ranges::copy(view.subspan<kNameAt, kNameBytes>(), r.name.begin());
        catalog.files.push_back(r);
    }

    std::vector<Record>& files = catalog.files;
    std::vector<Record*> by_offset;
    by_offset.reserve(files.size());
    for (Record& r : files)
        by_offset.push_back(&r);
    std::ranges::sort(by_offset, {}, &Record::offset);
    for (std::size_t i = 0; i < by_offset.size(); ++i) {
        const infra::ByteOffset next = i + 1 < by_offset.size() ? by_offset[i + 1]->offset
                                                                : tape.end();
        const ProgramSize gap = ProgramSize::low_word(next - by_offset[i]->offset);
        if (gap < by_offset[i]->size)
            by_offset[i]->size = gap;
    }
    for (Record& r : files)
        r.size = r.size.with_load_address();

    if (files.size() > kMostFiles)
        files.resize(kMostFiles);
    proto::BlockCount sectors{};
    for (std::size_t i = 0; i < files.size(); ++i) {
        sectors += files[i].size.file_sectors();
        if (sectors > kFileSectors) {
            files.resize(i);
            break;
        }
    }
    if (files.empty())
        return std::nullopt;
    return catalog;
}

void store_name(const Name& name, std::span<std::byte, kNameBytes> out) noexcept {
    std::ranges::copy(name, out.begin());
    for (std::size_t o = kNameBytes; o-- > 0;) {
        if (out[o] != kTapePad)
            break;
        out[o] = kDiskPad;
    }
}

class DiskWriter {
public:
    DiskWriter() : image_(kImageBytes.zeroed<std::vector<std::byte>>()) {}

    void write(proto::FileOffset at, std::span<const std::byte> src) noexcept {
        std::ranges::copy(src, infra::affine::from(std::span<std::byte>{image_}, at).begin());
    }

    [[nodiscard]] std::vector<std::byte> take() noexcept { return std::move(image_); }

private:
    std::vector<std::byte> image_;
};

[[nodiscard]] bool write_file(const Record& r, TapeReader& tape, Place& at, DiskWriter& disk) {
    enum Layout : std::size_t { kLinkBytes = 2, kAddressBytes = 2 };

    tape.seek(r.offset);
    bool first = true;
    for (infra::ByteCount done{}; done < r.size.bytes();
         done += infra::ByteCount{kFileSectorBytes}) {
        const proto::FileOffset home = at.offset();
        at.advance_file();
        at.step_over_directory();

        Sector sector;
        sector.fill(kDiskPad);
        const infra::ByteCount count = std::min(
            infra::ByteCount{kFileSectorBytes},
            r.size.bytes() - done
        );
        const bool more = done + infra::ByteCount{kFileSectorBytes} < r.size.bytes();

        const ChainLink link = more ? ChainLink::to(at) : ChainLink::last(count);
        link.store(std::span<std::byte>{sector}.first<kLinkBytes>());
        infra::ByteOffset body{kLinkBytes};
        infra::ByteCount want = count;
        if (first) {
            if (count < infra::ByteCount{kAddressBytes})
                return false;
            r.load_address.store(std::span<std::byte>{sector}.subspan<kLinkBytes, kAddressBytes>());
            body += infra::ByteCount{kAddressBytes};
            want -= infra::ByteCount{kAddressBytes};
        }
        if (tape.read(
                    infra::affine::first(
                        infra::affine::from(std::span<std::byte>{sector}, body),
                        want
                    )
            )
                .empty())
            return false;
        disk.write(home, sector);
        first = false;
    }
    return true;
}

[[nodiscard]] Sector make_bam(const Name& tape_name, const Heads& heads) noexcept {
    enum Bam : std::size_t {
        kDosVersionAt = 0x02,
        kEntriesAt = 0x04,
        kEntryBytes = BamEntry::kBytes,
        kNameAt = 0x90,
        kNameEnd = 0xA0,
        kIdAt = 0xA2,
        kIdPadAt = kIdAt + 2,
        kDosTypeAt = 0xA5,
        kTailPadAt = 0xA7,
        kTailPadEnd = 0xAB,
    };

    Sector bam{};
    infra::IndexedArray<BamEntry, kTracks, gcr::FullTrack> entries{};
    const auto free_sector = [&entries](Place p) {
        entries[p.track.on_first_side()].free(p.sector);
    };
    ChainLink::to(Place::kFirstDir).store(std::span<std::byte>{bam}.first<2>());
    bam[kDosVersionAt] = std::byte{'A'};
    Place file = heads.file;
    for (;;) {
        file.advance_file();
        if (file == Place::kFirstFile)
            break;
        if (file.on_directory_track())
            continue;
        free_sector(file);
    }
    Place dir = heads.dir;
    for (;;) {
        dir.advance_dir();
        if (dir == Place::kFirstDir)
            break;
        if (dir == Place::kBam)
            continue;
        free_sector(dir);
    }
    infra::ByteOffset entry_at{kEntriesAt};
    for (const BamEntry& entry : entries) {
        entry.store(infra::affine::from(std::span<std::byte>{bam}, entry_at).first<kEntryBytes>());
        entry_at += infra::ByteCount{kEntryBytes};
    }
    store_name(tape_name, std::span<std::byte>{bam}.subspan<kNameAt, kNameBytes>());
    std::ranges::fill(std::span<std::byte>{bam}.subspan(kNameEnd, kIdAt - kNameEnd), kDiskPad);
    bam[kIdAt] = std::byte{'0'};
    bam[kIdAt + 1] = std::byte{'0'};
    bam[kIdPadAt] = kDiskPad;
    bam[kDosTypeAt] = std::byte{'2'};
    bam[kDosTypeAt + 1] = std::byte{'A'};
    std::ranges::fill(
        std::span<std::byte>{bam}.subspan(kTailPadAt, kTailPadEnd - kTailPadAt),
        kDiskPad
    );
    return bam;
}

}

std::optional<std::vector<std::byte>> to_d64(std::span<const std::byte> tape_bytes) {
    enum Entry : std::size_t {
        kTypeAt = 0x02,
        kFirstTrackAt = 0x03,
        kFirstSectorAt = 0x04,
        kNameAt = 0x05,
        kSectorsAt = 0x1E,
    };

    TapeReader tape{tape_bytes};
    const std::optional<Catalog> catalog = read_catalog(tape);
    if (!catalog)
        return std::nullopt;

    DiskWriter disk;
    Heads heads{.file = Place::kFirstFile, .dir = Place::kFirstDir};
    std::size_t entry = 0;
    for (std::size_t i = 0; i < catalog->files.size(); ++i) {
        const Record& r = catalog->files[i];
        if (entry == 0 && i != 0) {

            const proto::FileOffset full = heads.dir.offset();
            do {
                heads.dir.advance_dir();
            } while (heads.dir == Place::kBam);
            std::array<std::byte, 2> link{};
            ChainLink::to(heads.dir).store(link);
            disk.write(full, link);
            if (heads.dir == Place::kFirstDir)
                return std::nullopt;
        }

        std::array<std::byte, kDirEntryBytes> row{};

        (entry == 0 ? ChainLink::kDirEnd : ChainLink{}).store(std::span<std::byte>{row}.first<2>());
        row[kTypeAt] = kProgram;
        row[kFirstTrackAt] = heads.file.track_byte();
        row[kFirstSectorAt] = heads.file.sector_byte();
        store_name(r.name, std::span<std::byte>{row}.subspan<kNameAt, kNameBytes>());
        r.size.store_file_blocks(std::span<std::byte>{row}.subspan<kSectorsAt, 2>());
        disk.write(heads.dir.offset() + proto::FileSize{entry * kDirEntryBytes}, row);
        entry = (entry + 1) % kDirEntriesPerSector;

        if (!write_file(r, tape, heads.file, disk))
            return std::nullopt;
    }

    disk.write(Place::kBam.offset(), make_bam(catalog->tape_name, heads));
    return disk.take();
}

}
