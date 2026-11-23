// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/woz35_codec.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "infra/byte_count.h"
#include "infra/indexed_array.h"
#include "infra/span_cut.h"
#include "svc/six_and_two.h"
#include "svc/woz_format.h"

namespace mister::svc {
namespace {

using infra::affine::first;
using infra::affine::first_clamped;
using infra::affine::from;
using infra::affine::operator+;
using infra::affine::operator-;
using Bytes = std::span<const std::byte>;
using Out = std::span<std::byte>;

constexpr proto::FileSize kDiskBytes{819'200};
constexpr proto::FileSize kBlockBytes = woz::kBlock.bytes_of(proto::BlockCount{1});
constexpr std::size_t kZones = 5;
constexpr std::size_t kMaxSectors = 12;

struct Zone35 {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr Zone35 next() const noexcept {
        return Zone35{static_cast<std::uint8_t>(v + 1u)};
    }

    friend constexpr bool operator==(Zone35, Zone35) = default;
    friend constexpr auto operator<=>(Zone35, Zone35) = default;
};

inline constexpr Zone35 kLastZone{static_cast<std::uint8_t>(kZones - 1)};

struct Sector35 {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr proto::BlockCount before() const noexcept {
        return proto::BlockCount{v};
    }

    [[nodiscard]] constexpr std::byte six() const noexcept { return std::byte{v}; }

    [[nodiscard]] static constexpr std::optional<Sector35> of_six(std::byte disk) noexcept {
        const std::optional<std::byte> six = six_of_disk_byte(disk);
        if (!six)
            return std::nullopt;
        return Sector35{std::to_integer<std::uint8_t>(*six)};
    }

    friend constexpr bool operator==(Sector35, Sector35) = default;
    friend constexpr auto operator<=>(Sector35, Sector35) = default;
};

struct Track35 {
    std::uint8_t v = 0;
    static const Track35 kEnd;

    [[nodiscard]] constexpr Track35 next() const noexcept {
        return Track35{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr Zone35 zone() const noexcept;
    [[nodiscard]] constexpr proto::BlockCount sectors() const noexcept;

    [[nodiscard]] constexpr std::span<const Sector35> sector_order() const noexcept;

    [[nodiscard]] constexpr proto::Lba first_block() const noexcept;

    [[nodiscard]] constexpr std::byte track_six() const noexcept;
    [[nodiscard]] constexpr std::byte side_six() const noexcept;

    [[nodiscard]] static constexpr std::optional<Track35> of(woz::TrackSlot slot) noexcept {
        if (!(slot < woz::TrackSlot::kEnd))
            return std::nullopt;
        return Track35{slot.v};
    }

    [[nodiscard]] constexpr woz::TrackSlot slot() const noexcept {
        return woz::TrackSlot{static_cast<std::uint8_t>(v)};
    }

    friend constexpr bool operator==(Track35, Track35) = default;
    friend constexpr auto operator<=>(Track35, Track35) = default;
};

inline constexpr Track35 Track35::kEnd{160};

constexpr std::array<Track35, kZones + 1>
    kZoneStart{Track35{0}, Track35{32}, Track35{64}, Track35{96}, Track35{128}, Track35{160}};

constexpr auto kSectorsPerZone = [] {
    infra::IndexedArray<proto::BlockCount, kZones, Zone35> table{};
    constexpr std::array<std::uint8_t, kZones> counts{12, 11, 10, 9, 8};
    for (Zone35 z{}; z <= kLastZone; z = z.next())
        table[z] = proto::BlockCount{infra::at_index(counts, z)};
    return table;
}();

constexpr auto kPhysToLogical = [] {
    infra::IndexedArray<std::array<Sector35, kMaxSectors>, kZones, Zone35> table{};
    constexpr std::array<std::array<std::uint8_t, kMaxSectors>, kZones> rows{{
        {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11},
        {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5},
        {0, 5, 1, 6, 2, 7, 3, 8, 4, 9},
        {0, 5, 1, 6, 2, 7, 3, 8, 4},
        {0, 4, 1, 5, 2, 6, 3, 7},
    }};
    for (Zone35 z{}; z <= kLastZone; z = z.next())
        for (std::size_t i = 0; i < kMaxSectors; ++i)
            table[z][i] = Sector35{infra::at_index(rows, z)[i]};
    return table;
}();

[[nodiscard]] constexpr bool zones_fit() noexcept {
    for (Zone35 z{}; z <= kLastZone; z = z.next())
        if (kSectorsPerZone[z] > proto::BlockCount{kMaxSectors})
            return false;
    return true;
}

static_assert(zones_fit(), "iigs_fmt.cpp:575-608: every zone's sectors fit a physical-order row");

constexpr Zone35 Track35::zone() const noexcept {
    for (Zone35 z{}; z < kLastZone; z = z.next())
        if (v < infra::at_index(kZoneStart, z.next()).v)
            return z;
    return kLastZone;
}

constexpr proto::BlockCount Track35::sectors() const noexcept {
    return kSectorsPerZone[zone()];
}

constexpr std::span<const Sector35> Track35::sector_order() const noexcept {
    return infra::affine::first(std::span<const Sector35>{kPhysToLogical[zone()]}, sectors());
}

constexpr proto::Lba Track35::first_block() const noexcept {
    proto::Lba base{};
    for (Track35 k{}; k < *this; k = k.next())
        base = base + k.sectors();
    return base;
}

constexpr std::byte Track35::track_six() const noexcept {
    return std::byte{static_cast<std::uint8_t>(v >> 1)};
}

constexpr std::byte Track35::side_six() const noexcept {
    constexpr unsigned kSideShift = 5;
    constexpr unsigned kTrackHighShift = 6;
    const unsigned side = v & 1u;
    const unsigned cylinder = v >> 1;
    return std::byte{
        static_cast<std::uint8_t>((side << kSideShift) | (cylinder >> kTrackHighShift))
    };
}

static_assert(
    woz::kBlock.position_of(Track35::kEnd.first_block()) == proto::kFileStart + kDiskBytes,
    "iigs_fmt.cpp:575-608: the five zones hold exactly the 1,600 blocks of an 800K disk"
);

[[nodiscard]] Bytes block_in(Bytes disk, proto::Lba b) noexcept {
    return first(from(disk, woz::kBlock.position_of(b) - proto::kFileStart), kBlockBytes);
}

constexpr std::size_t kTagBytes = 12;
constexpr std::size_t kFieldData = 524;
constexpr std::size_t kGroups = 175;
constexpr std::size_t kFieldBytes = 699;

struct DataField35 {

    static constexpr std::size_t kChecksumSixes = 4;

    std::array<std::byte, kFieldBytes + kChecksumSixes> v{};

    [[nodiscard]] static DataField35 encode(std::span<const std::byte> block) noexcept {
        constexpr std::size_t kThird = 2;
        std::array<std::uint8_t, kFieldData> data{};
        for (std::size_t i = 0; i < block.size(); ++i)
            data[kTagBytes + i] = std::to_integer<std::uint8_t>(block[i]);
        Checksum35 checksum{};
        DataField35 field{};
        std::size_t di = 0;
        std::size_t at = 0;
        while (di < kFieldData) {
            std::array<std::uint8_t, 3> group{};
            group[0] = data[di++];
            group[1] = data[di++];
            const bool full = di < kFieldData;
            if (full)
                group[kThird] = data[di++];
            const std::array<std::byte, 4> six = checksum.mix(Group62{group}, full).sixes();
            const std::size_t width = full ? six.size() : six.size() - 1;
            for (std::size_t i = 0; i < width; ++i)
                field.v[at++] = disk_byte_of_six(six[i]);
        }
        for (const std::byte six : checksum.sixes())
            field.v[at++] = disk_byte_of_six(six);
        return field;
    }

    [[nodiscard]] static std::optional<std::array<std::byte, 512>> decode(
        std::span<const std::byte, kFieldBytes> gcr
    ) noexcept {
        constexpr std::size_t kThird = 2;
        Checksum35 checksum{};
        auto p = gcr.begin();
        std::array<std::uint8_t, kFieldData> data{};
        std::size_t di = 0;
        for (std::size_t i = 0; i < kGroups; ++i) {
            const bool full = i + 1 < kGroups;
            const std::optional<std::byte> top = six_of_disk_byte(*p++);
            if (!top)
                return std::nullopt;
            std::array<std::byte, 3> low{};
            const std::optional<std::byte> first = six_of_disk_byte(*p++);
            if (!first)
                return std::nullopt;
            low[0] = *first;
            const std::optional<std::byte> second = six_of_disk_byte(*p++);
            if (!second)
                return std::nullopt;
            low[1] = *second;
            if (full) {
                const std::optional<std::byte> third = six_of_disk_byte(*p++);
                if (!third)
                    return std::nullopt;
                low[kThird] = *third;
            }
            std::array<std::uint8_t, 3> group{};
            checksum.unmix(Group62::of_sixes(*top, low), full).scatter(group);
            data[di++] = group[0];
            data[di++] = group[1];
            if (di == kFieldData)
                break;
            data[di++] = group[kThird];
        }
        std::array<std::byte, 512> out{};
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = std::byte{data[kTagBytes + i]};
        return out;
    }

private:
    struct Group62 {
        std::array<std::uint8_t, 3> v{};

        [[nodiscard]] std::byte top_six() const noexcept {
            constexpr unsigned kTop = 0xC0;
            constexpr unsigned kFirstTop = 2;
            constexpr unsigned kSecondTop = 4;
            constexpr unsigned kThirdTop = 6;
            constexpr std::size_t kThird = 2;
            return std::byte{static_cast<std::uint8_t>(
                ((v[0] & kTop) >> kFirstTop)
                | ((v[1] & kTop) >> kSecondTop)
                | ((v[kThird] & kTop) >> kThirdTop)
            )};
        }

        [[nodiscard]] static Group62 of_sixes(
            std::byte top,
            std::array<std::byte, 3> lows
        ) noexcept {
            constexpr unsigned kTop = 0xC0;
            constexpr unsigned kFirstTop = 2;
            constexpr unsigned kSecondTop = 4;
            constexpr unsigned kThirdTop = 6;
            constexpr std::size_t kThird = 2;
            const std::uint8_t high = std::to_integer<std::uint8_t>(top);
            const auto first = static_cast<std::uint8_t>(
                ((high << kFirstTop) & kTop) | std::to_integer<std::uint8_t>(lows[0])
            );
            const auto second = static_cast<std::uint8_t>(
                ((high << kSecondTop) & kTop) | std::to_integer<std::uint8_t>(lows[1])
            );
            const auto third = static_cast<std::uint8_t>(
                ((high << kThirdTop) & kTop) | std::to_integer<std::uint8_t>(lows[kThird])
            );
            return Group62{{first, second, third}};
        }

        [[nodiscard]] std::array<std::byte, 4> sixes() const noexcept {
            std::array<std::byte, 4> out{};
            out[0] = top_six();
            for (std::size_t i = 0; i < v.size(); ++i)
                out[i + 1] = std::byte{v[i]};
            return out;
        }

        void scatter(std::span<std::uint8_t> out) const noexcept {
            for (std::size_t i = 0; i < v.size(); ++i)
                out[i] = v[i];
        }
    };

    struct Checksum35 {
        std::array<unsigned, 3> v{};

        [[nodiscard]] Group62 mix(Group62 data, bool full) noexcept {
            constexpr unsigned kByte = 0xFF;
            constexpr unsigned kCarry = 0x100;
            constexpr std::size_t kThird = 2;
            const std::array<std::uint8_t, 3> in = data.v;
            std::array<std::uint8_t, 3> out{};
            v[0] = (v[0] & kByte) << 1;
            if (v[0] & kCarry)
                ++v[0];
            unsigned byte = in[0];
            v[kThird] += byte;
            if (v[0] & kCarry) {
                ++v[kThird];
                v[0] &= kByte;
            }
            out[0] = static_cast<std::uint8_t>(byte ^ v[0]);
            byte = in[1];
            v[1] += byte;
            if (v[kThird] > kByte) {
                ++v[1];
                v[kThird] &= kByte;
            }
            out[1] = static_cast<std::uint8_t>(byte ^ v[kThird]);
            if (full) {
                byte = in[kThird];
                v[0] += byte;
                if (v[1] > kByte) {
                    ++v[0];
                    v[1] &= kByte;
                }
                out[kThird] = static_cast<std::uint8_t>(byte ^ v[1]);
            }
            return Group62{out};
        }

        [[nodiscard]] Group62 unmix(Group62 coded, bool full) noexcept {
            constexpr unsigned kByte = 0xFF;
            constexpr unsigned kCarry = 0x100;
            constexpr std::size_t kThird = 2;
            const std::array<std::uint8_t, 3> in = coded.v;
            std::array<std::uint8_t, 3> out{};
            v[0] = (v[0] & kByte) << 1;
            if (v[0] & kCarry)
                ++v[0];
            const auto first = static_cast<std::uint8_t>(in[0] ^ v[0]);
            v[kThird] += first;
            if (v[0] & kCarry) {
                ++v[kThird];
                v[0] &= kByte;
            }
            out[0] = first;
            const auto second = static_cast<std::uint8_t>(in[1] ^ v[kThird]);
            v[1] += second;
            if (v[kThird] >= kCarry) {
                ++v[1];
                v[kThird] &= kByte;
            }
            out[1] = second;
            if (full) {
                const auto third = static_cast<std::uint8_t>(in[kThird] ^ v[1]);
                v[0] += third;
                if (v[1] >= kCarry) {
                    ++v[0];
                    v[1] &= kByte;
                }
                out[kThird] = third;
            }
            return Group62{out};
        }

        [[nodiscard]] std::array<std::byte, 4> sixes() const noexcept {
            constexpr unsigned kTop = 0xC0;
            constexpr unsigned kFirstTop = 6;
            constexpr unsigned kSecondTop = 4;
            constexpr unsigned kThirdTop = 2;
            constexpr std::size_t kThird = 2;
            constexpr std::size_t kFourth = 3;
            std::array<std::byte, 4> out{};
            out[0] = std::byte{static_cast<std::uint8_t>(
                ((v[0] & kTop) >> kFirstTop)
                | ((v[1] & kTop) >> kSecondTop)
                | ((v[kThird] & kTop) >> kThirdTop)
            )};
            out[1] = std::byte{static_cast<std::uint8_t>(v[kThird])};
            out[kThird] = std::byte{static_cast<std::uint8_t>(v[1])};
            out[kFourth] = std::byte{static_cast<std::uint8_t>(v[0])};
            return out;
        }
    };
};

struct SyncRun {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr SyncRun filling(infra::ByteCount gap) noexcept {
        constexpr std::size_t kByteBits = CHAR_BIT;
        constexpr std::size_t kSyncBits = 10;
        return SyncRun{static_cast<std::uint16_t>(gap * kByteBits / infra::ByteCount{kSyncBits})};
    }

    [[nodiscard]] constexpr SyncRun next() const noexcept {
        return SyncRun{static_cast<std::uint16_t>(v + 1u)};
    }

    friend constexpr bool operator==(SyncRun, SyncRun) = default;
    friend constexpr auto operator<=>(SyncRun, SyncRun) = default;
};

inline constexpr SyncRun kGap1 = SyncRun::filling(infra::ByteCount{500});
inline constexpr SyncRun kGap3 = SyncRun::filling(infra::ByteCount{53});
inline constexpr SyncRun kAddress{4};

inline constexpr std::byte kSyncByte{0xFF};

class BitWriter {
public:
    explicit BitWriter(Out buf) noexcept : buf_(buf) {}

    void put(std::byte byte) noexcept {
        for (unsigned i = CHAR_BIT; i-- > 0;)
            bit_((std::to_integer<unsigned>(byte) >> i & 1u) != 0u);
    }

    void put(Bytes bytes) noexcept {
        for (const std::byte byte : bytes)
            put(byte);
    }

    void put(const DataField35& field) noexcept {
        for (const std::byte byte : field.v)
            put(byte);
    }

    void put_six(std::byte six) noexcept { put(disk_byte_of_six(six)); }

    void sync(SyncRun run) noexcept {
        for (SyncRun i{}; i < run; i = i.next()) {
            bit_(false);
            bit_(false);
            put(kSyncByte);
        }
    }

    [[nodiscard]] woz::BitCount count() const noexcept { return cursor_; }

private:
    void bit_(bool set) noexcept {
        const infra::ByteOffset at = cursor_.byte_at();
        if (infra::affine::in_bounds(infra::ByteCount{buf_.size()}, at)) {
            if (set) {
                infra::at_index(buf_, at) |= cursor_.mask();
            } else {
                infra::at_index(buf_, at) &= ~cursor_.mask();
            }
        }
        cursor_ = cursor_ + woz::BitCount{1};
    }

    Out buf_;
    woz::BitCount cursor_{};
};

constexpr std::array<std::byte, 3> kAddressMark{std::byte{0xD5}, std::byte{0xAA}, std::byte{0x96}};
constexpr std::array<std::byte, 3> kDataMark{std::byte{0xD5}, std::byte{0xAA}, std::byte{0xAD}};
constexpr std::array<std::byte, 2> kFieldEnd{std::byte{0xDE}, std::byte{0xAA}};

constexpr std::byte kFormat{0x22};

void encode_track(BitWriter& w, Track35 nt, Bytes disk) noexcept {
    const std::span<const Sector35> order = nt.sector_order();
    const std::byte track_six = nt.track_six();
    const std::byte side_six = nt.side_six();
    const proto::Lba base = nt.first_block();
    w.put(kSyncByte);
    w.sync(kGap1);
    for (const Sector35 lsec : order) {
        w.put(kSyncByte);
        w.put(kAddressMark);
        w.put_six(track_six);
        w.put_six(lsec.six());
        w.put_six(side_six);
        w.put_six(kFormat);
        w.put_six(track_six ^ lsec.six() ^ side_six ^ kFormat);
        w.put(kFieldEnd);
        w.put(kSyncByte);
        w.sync(kAddress);
        w.put(kSyncByte);
        w.put(kDataMark);
        w.put_six(lsec.six());
        w.put(DataField35::encode(block_in(disk, base + lsec.before())));
        w.put(kFieldEnd);
        if (lsec != order.back()) {
            w.put(kSyncByte);
            w.put(kSyncByte);
            w.put(kSyncByte);
            w.sync(kGap3);
        }
    }
}

struct ReadLatch {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr ReadLatch shifted_in(bool bit) const noexcept {
        return ReadLatch{static_cast<std::uint8_t>((v << 1) | (bit ? 1u : 0u))};
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        constexpr std::uint8_t kLatched = 0x80;
        return (v & kLatched) != 0;
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }
};

[[nodiscard]] infra::ByteCount frame_track(Bytes bits, woz::BitCount bit_count, Out out) noexcept {
    std::size_t n = 0;
    ReadLatch latch{};
    for (woz::BitCount i{}; i < bit_count; i = i + woz::BitCount{1}) {
        if (!infra::affine::in_bounds(infra::ByteCount{bits.size()}, i.byte_at()))
            break;
        latch = latch.shifted_in(i.bit_in(bits));
        if (latch.full()) {
            if (n < out.size())
                out[n++] = latch.byte();
            latch = ReadLatch{};
        }
    }
    return infra::ByteCount{n};
}

struct DecodedTrack {
    proto::Lba base{};
    proto::BlockCount blocks{};
    proto::BlockCount got{};
};

[[nodiscard]] std::optional<DecodedTrack> decode_track(Bytes woz, Track35 nt, Out disk) noexcept {
    const std::optional<Bytes> trks = woz::find_chunk(woz, woz::kTrks);
    if (!trks)
        return std::nullopt;
    const std::optional<woz::TrackEntry> e = woz::track_entry(*trks, nt.slot());
    if (!e || e->blocks == proto::BlockCount{} || e->bits.empty())
        return std::nullopt;
    const proto::FileOffset start = woz::kBlock.position_of(e->start);
    if (start + woz::kBlock.bytes_of(e->blocks) > proto::kFileStart + proto::FileSize::of(woz))
        return std::nullopt;
    constexpr std::size_t kFramed = 20 * 1024;
    std::array<std::byte, kFramed> bytes{};
    const infra::ByteCount framed = frame_track(
        from(woz, start - proto::kFileStart),
        e->bits,
        bytes
    );
    const Bytes framed_bytes = first(Bytes{bytes}, framed);
    DecodedTrack d{.base = nt.first_block(), .blocks = nt.sectors()};
    for (std::size_t i = 0; i + kDataMark.size() < framed_bytes.size() && d.got < d.blocks;) {
        if (!std::ranges::equal(framed_bytes.subspan(i, kDataMark.size()), kDataMark)) {
            ++i;
            continue;
        }
        const std::size_t field = i + kDataMark.size() + 1;
        if (field + kFieldBytes > framed_bytes.size())
            break;
        const std::optional<Sector35> lsec = Sector35::of_six(framed_bytes[i + kDataMark.size()]);
        if (lsec && lsec->before() < d.blocks) {
            if (const auto block = DataField35::decode(
                    framed_bytes.subspan(field).first<kFieldBytes>()
                )) {
                const proto::Lba at = d.base + lsec->before();
                std::ranges::copy(
                    *block,
                    from(disk, woz::kBlock.position_of(at) - proto::kFileStart).begin()
                );
                d.got += proto::BlockCount{1};
            }
        }
        i = field + kFieldBytes;
    }
    return d;
}

constexpr woz::InfoFields kInfoFields{
    .type = woz::DiskType::Inch35,
    .creator = "MiSTer IIgs easy-WOZ 3.5",
    .sides = std::byte{2},
    .bit_timing = std::byte{16}
};

[[nodiscard]] std::optional<std::vector<std::byte>> build_woz(Bytes disk) {
    constexpr std::size_t kTrackRoom = 16 * 1024;
    const auto end_of = [](proto::Lba block) {
        return (woz::kBlock.position_of(block) - proto::kFileStart).file_word<std::size_t>();
    };
    std::vector<std::byte> woz(end_of(woz::kFirstBitsBlock));
    woz::put_header(woz, kInfoFields);
    const Out tmap = woz::tmap_of(woz);
    for (std::uint8_t i = 0; i < tmap.size(); ++i)
        tmap[i] = std::byte{i};

    proto::Lba block = woz::kFirstBitsBlock;
    proto::BlockCount largest{};
    std::array<std::byte, kTrackRoom> room{};
    for (Track35 nt; nt < Track35::kEnd; nt = nt.next()) {
        std::ranges::fill(room, std::byte{});
        BitWriter w{room};
        encode_track(w, nt, disk);
        const woz::BitCount bits = w.count();
        const proto::FileSize bytes = bits.bytes_covering();
        const proto::BlockCount blocks = bytes.units_covering(woz::kBlock);
        woz.resize(end_of(block + blocks));
        std::ranges::copy(
            first(Bytes{room}, bytes),
            from(Out{woz}, woz::kBlock.position_of(block)).begin()
        );
        if (!woz::put_track_entry(
                woz::trks_of(woz),
                nt.slot(),
                woz::TrackEntry{.start = block, .blocks = blocks, .bits = bits}
            ))
            return std::nullopt;
        block += blocks;
        largest = std::max(largest, blocks);
    }
    if (!woz::put_largest_track(woz, largest))
        return std::nullopt;
    woz::put_track_data_size(woz, block - woz::kFirstBitsBlock);
    woz::seal(woz);
    return woz;
}

}

Woz35Codec::Woz35Codec(
    std::unique_ptr<IDiskImage> source,
    bool writes_back,
    std::vector<std::byte> woz
)
    : source_(std::move(source)),
      writes_back_(writes_back),
      woz_(std::move(woz)),
      blocks_(kDiskBytes.zeroed<std::vector<std::byte>>()) {}

Ex<std::unique_ptr<IDiskImage>> Woz35Codec::open(
    std::unique_ptr<IDiskImage> source,
    bool writes_back
) {
    TASTY_SEAT_BODY(Woz35Codec);
    auto disk = read_whole(*source);
    if (!disk)
        return std::unexpected(disk.error());

    if (proto::FileSize::of(std::span{*disk}) != kDiskBytes)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    std::optional<std::vector<std::byte>> woz = build_woz(*disk);
    if (!woz)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return std::unique_ptr<IDiskImage>{
        new Woz35Codec(std::move(source), writes_back, std::move(*woz))
    };
}

FileSize Woz35Codec::size() const noexcept {
    TASTY_SEAT_BODY(Woz35Codec);
    return FileSize::of(std::span{woz_});
}

bool Woz35Codec::writable() const noexcept {
    TASTY_SEAT_BODY(Woz35Codec);
    return writes_back_;
}

Ex<infra::ByteCount> Woz35Codec::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(Woz35Codec);
    if (!size().holds(at))
        return infra::ByteCount{};
    const Bytes rest = from(Bytes{woz_}, at);
    const Bytes served = rest.first(std::min(rest.size(), dst.size()));
    std::ranges::copy(served, dst.begin());
    return infra::ByteCount{served.size()};
}

Ex<infra::ByteCount> Woz35Codec::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(Woz35Codec);
    for (Bytes rest = src; !rest.empty();) {
        const Bytes act = first_clamped(rest, kBlockBytes);
        if (auto r = write_act_(at, act); !r)
            return std::unexpected(r.error());
        at += proto::FileSize::of(act);
        rest = rest.subspan(act.size());
    }
    return infra::ByteCount{src.size()};
}

Ex<void> Woz35Codec::write_act_(proto::FileOffset at, std::span<const std::byte> act) {
    if (size().clamp_write(at, kBlockBytes.file_word<std::size_t>())
        == kBlockBytes.file_word<std::size_t>())
        std::ranges::copy(act, from(Out{woz_}, at - proto::kFileStart).begin());
    if (!writes_back_)
        return {};
    const proto::Lba lba = proto::Lba{} + woz::kBlock.blocks_in(at - proto::kFileStart);
    const std::optional<woz::TrackSlot> slot = woz::track_holding(woz_, lba);
    if (!slot)
        return {};

    const std::optional<Track35> track = Track35::of(*slot);
    if (!track)
        return {};
    const std::optional<DecodedTrack> d = decode_track(woz_, *track, blocks_);
    if (!d || d->got == proto::BlockCount{})
        return {};
    const proto::FileOffset from_at = woz::kBlock.position_of(d->base);
    const Bytes span = first(
        from(Bytes{blocks_}, from_at - proto::kFileStart),
        woz::kBlock.bytes_of(d->blocks)
    );
    auto w = source_->write_at(from_at, span);
    if (!w)
        return std::unexpected(w.error());
    return {};
}

}
