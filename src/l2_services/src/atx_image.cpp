// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/atx_image.h"

#include <algorithm>
#include <concepts>
#include <cstring>
#include <utility>

#include "hal/reg_field.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "os/raw_monotonic_clock.h"
#include "svc/atx_drive.h"

namespace mister::svc {

namespace {

constexpr infra::ByteCount kSectorGapBytes{23};
constexpr AtxUnits kUnitsPerByteSingle{8};
constexpr AtxUnits kUnitsPerByteWide{4};
constexpr AtxUnits kReadTail{2};

constexpr std::array<std::byte, 4> kSignature =
    {std::byte{'A'}, std::byte{'T'}, std::byte{'8'}, std::byte{'X'}};
constexpr AtxLength kFileHeader{48};
constexpr AtxLength kTrackHeader{32};
constexpr AtxLength kSectorList{8};
constexpr AtxLength kSectorHeader{8};
constexpr AtxLength kChunk{8};
constexpr hal::RegField<std::uint16_t> kTrackFlagMedium{1, 0x1};
constexpr std::byte kChunkWeak{0x10};
constexpr std::byte kChunkLong{0x11};

constexpr infra::ByteCount kPadBytes{128};
constexpr std::byte kPad{0xFF};

constexpr AtxTrack kTwinTrack{38};
constexpr AtxSectorNumber kTwinSector{25};
constexpr AtxSectorNumber kTwinNumber{23};
constexpr AtxUnits kTwinTime{12066};
constexpr AtxUnits kTwinKeepTime{12355};
constexpr AtxLength kTwinSkip{36};
constexpr SectorCount kSectorsSingle{18};
constexpr SectorCount kSectorsMedium{26};
constexpr SectorSize kBytesNarrow{128};
constexpr SectorSize kBytesDouble{256};
constexpr SectorCount kCountSingle{720};
constexpr SectorCount kCountMedium{1040};
constexpr infra::ByteCount kWidest{256};

class Fields {
public:
    explicit Fields(std::span<const std::byte> bytes) noexcept : rest_(bytes) {}

    template <std::integral T>
    [[nodiscard]] T take() noexcept {
        const T v = infra::load_le<T>(rest_.first<sizeof(T)>());
        rest_ = infra::affine::from(rest_, infra::ByteCount{sizeof(T)});
        return v;
    }

    void skip(infra::ByteCount n) noexcept { rest_ = infra::affine::from(rest_, n); }

private:
    std::span<const std::byte> rest_;
};

struct TrackHeader {
    AtxLength size{};
    AtxTrack number{};
    AtxEntryCount sectors{};
    AtxImage::Density marked = AtxImage::Density::Single;
    AtxLength header_bytes{};

    [[nodiscard]] static TrackHeader decode(std::span<const std::byte> b) noexcept {
        Fields f{b};
        TrackHeader h{};
        h.size = AtxLength{f.take<std::uint32_t>()};
        f.skip(infra::ByteCount{sizeof(std::uint16_t) + sizeof(std::uint16_t)});
        h.number = AtxTrack{f.take<std::uint8_t>()};
        f.skip(infra::ByteCount{sizeof(std::uint8_t)});
        h.sectors = AtxEntryCount{f.take<std::uint16_t>()};
        f.skip(infra::ByteCount{sizeof(std::uint16_t) + sizeof(std::uint16_t)});
        const auto flags = f.take<std::uint16_t>();
        h.marked = kTrackFlagMedium.test(flags) ? AtxImage::Density::Medium
                                                : AtxImage::Density::Single;
        f.skip(infra::ByteCount{sizeof(std::uint16_t)});
        h.header_bytes = AtxLength{f.take<std::uint32_t>()};
        return h;
    }
};

struct SectorHeader {
    AtxSectorNumber number{};
    FdcStatus status{};
    AtxUnits time{};
    std::optional<AtxLength> data;

    [[nodiscard]] static SectorHeader decode(std::span<const std::byte> b) noexcept {
        Fields f{b};
        SectorHeader h{};
        h.number = AtxSectorNumber{f.take<std::uint8_t>()};
        h.status = FdcStatus{f.take<std::uint8_t>()};
        h.time = AtxUnits{f.take<std::uint16_t>()};
        const std::uint32_t data = f.take<std::uint32_t>();
        if (data != 0)
            h.data = AtxLength{data};
        return h;
    }
};

enum class AtxChunkKind : std::uint8_t { Other, Weak, Long };

struct Chunk {
    AtxLength size{};
    AtxChunkKind kind = AtxChunkKind::Other;
    AtxEntry sector{};
    AtxChunkData data{};

    [[nodiscard]] static Chunk decode(std::span<const std::byte> b) noexcept {
        Fields f{b};
        Chunk c{};
        c.size = AtxLength{f.take<std::uint32_t>()};
        const std::byte type{f.take<std::uint8_t>()};
        c.kind = type == kChunkWeak ? AtxChunkKind::Weak
            : type == kChunkLong    ? AtxChunkKind::Long
                                    : AtxChunkKind::Other;
        c.sector = AtxEntry{f.take<std::uint8_t>()};
        c.data = AtxChunkData{f.take<std::uint16_t>()};
        return c;
    }
};

struct AtxFileHeader {
    AtxImage::Density density = AtxImage::Density::Single;
    AtxOffset start{};

    [[nodiscard]] static Ex<AtxFileHeader> decode(
        std::span<const std::byte, kFileHeader.v> head
    ) noexcept {
        constexpr std::uint16_t kVersion = 1;
        constexpr std::uint8_t kDensitySingle = 0;
        constexpr std::uint8_t kDensityMedium = 1;
        constexpr std::uint8_t kDensityDouble = 2;
        Fields f{head};
        f.skip(infra::ByteCount{kSignature.size()});
        const auto version = f.take<std::uint16_t>();
        const auto min_version = f.take<std::uint16_t>();

        f.skip(
            infra::ByteCount{
                sizeof(std::uint16_t)
                + sizeof(std::uint16_t)
                + sizeof(std::uint32_t)
                + sizeof(std::uint16_t)
            }
        );
        const auto density = f.take<std::uint8_t>();

        f.skip(
            infra::ByteCount{
                sizeof(std::uint8_t)
                + sizeof(std::uint32_t)
                + sizeof(std::uint16_t)
                + sizeof(std::uint16_t)
            }
        );
        const auto start = f.take<std::uint32_t>();
        if (!std::ranges::equal(std::span{head}.first<kSignature.size()>(), kSignature)
            || version != kVersion
            || min_version != kVersion)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), density});
        AtxFileHeader header{};
        switch (density) {
            case kDensitySingle:
                header.density = AtxImage::Density::Single;
                break;
            case kDensityMedium:
                header.density = AtxImage::Density::Medium;
                break;
            case kDensityDouble:
                header.density = AtxImage::Density::Double;
                break;
            default:
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), density});
        }
        header.start = AtxOffset{start};
        return header;
    }
};

class Timeline {
public:
    explicit Timeline(os::RawMonotonicClock::time_point at) noexcept
        : start_(std::chrono::duration_cast<std::chrono::microseconds>(os::since_boot(at))),
          now_(start_) {}

    void wait(std::chrono::microseconds us) noexcept { now_ += us; }

    void stamp() noexcept {
        stamp_ = now_;
        angle_ = AtxUnits{std::chrono::floor<AtxUnits>(now_).count() % kFullRotation.count()};
    }

    void wait_from_stamp(std::chrono::microseconds d) noexcept {
        const std::chrono::microseconds elapsed = now_ - stamp_;
        if (elapsed <= d)
            now_ += d - elapsed;
    }

    [[nodiscard]] AtxUnits angle() const noexcept { return angle_; }

    [[nodiscard]] std::chrono::microseconds busy() const noexcept { return now_ - start_; }

private:
    std::chrono::microseconds start_;
    std::chrono::microseconds now_;
    std::chrono::microseconds stamp_{};
    AtxUnits angle_{};
};

[[nodiscard]] bool read_all(IFile& file, AtxOffset at, std::span<std::byte> dst) {
    return file.read_exact_at(at.file_offset(), dst).has_value();
}

[[nodiscard]] bool write_all(IFile& file, AtxOffset at, std::span<const std::byte> src) {
    return file.write_all_at(at.file_offset(), src).has_value();
}

[[nodiscard]] std::chrono::microseconds seek_time(
    TrackMove move,
    const IAtxDriveModel& model
) noexcept {
    if (move.from == move.to)
        return std::chrono::microseconds{};
    const AtxDrive& drive = model.drive();
    return model.steps(move).times(drive.step) + drive.settle;
}

[[nodiscard]] AtxUnits read_units(SectorSize bytes, AtxImage::Density density) noexcept {
    constexpr AtxUnits kUnitWrap{65536};
    const AtxUnits per_byte = density == AtxImage::Density::Single ? kUnitsPerByteSingle
                                                                   : kUnitsPerByteWide;
    const AtxUnits units = (kSectorGapBytes + bytes.bytes()).passing_time(per_byte) + kReadTail;
    return AtxUnits{units.count() % kUnitWrap.count()};
}

}

Ex<std::unique_ptr<ISectorImage>> AtxImage::open(std::unique_ptr<IFile> file, bool writable) {
    TASTY_SEAT_BODY(AtxImage);
    if (file == nullptr)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    std::array<std::byte, kFileHeader.v> head{};
    if (auto got = file->read_exact_at(proto::kFileStart, head); !got)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const Ex<AtxFileHeader> header = AtxFileHeader::decode(head);
    if (!header)
        return std::unexpected(header.error());

    TrackPlaces tracks{};
    AtxOffset at = header->start;
    for (auto& track : tracks) {
        std::array<std::byte, kTrackHeader.v> record{};
        if (!read_all(*file, at, record))
            break;
        if (at != AtxOffset{})
            track = at;
        at = at + TrackHeader::decode(record).size;
    }
    return std::unique_ptr<ISectorImage>(
        new AtxImage(std::move(file), writable, header->density, tracks)
    );
}

SectorGeometry AtxImage::geometry() const noexcept {
    TASTY_SEAT_BODY(AtxImage);
    return SectorGeometry{
        .size = sector_bytes_(),
        .count = SectorCount{density_ == Density::Medium ? kCountMedium : kCountSingle}
    };
}

bool AtxImage::writable() const noexcept {
    TASTY_SEAT_BODY(AtxImage);
    return writable_;
}

Ex<SectorRead> AtxImage::read_sector(const SectorAsk& ask, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(AtxImage);
    if (!sector_bytes_().fits(dst))
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    std::ranges::fill(sector_bytes_().of(dst), std::byte{});
    return transfer_(ask, Transfer{.op = Op::Read, .into = dst});
}

Ex<SectorRead> AtxImage::write_sector(const SectorAsk& ask, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(AtxImage);
    if (!writable_ || !sector_bytes_().fits(src))
        return std::unexpected(ask.sector.refusal(Errc::contract, ERR_SITE()));
    SectorRead done = transfer_(ask, Transfer{.op = Op::Write, .from = src});
    if (!file_->flush())
        done.outcome = SectorOutcome::Faulty;
    return done;
}

Ex<void> AtxImage::clear() {
    TASTY_SEAT_BODY(AtxImage);
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

SectorCount AtxImage::sectors_per_track_() const noexcept {
    return density_ == Density::Medium ? kSectorsMedium : kSectorsSingle;
}

SectorSize AtxImage::sector_bytes_() const noexcept {
    return density_ == Density::Double ? kBytesDouble : kBytesNarrow;
}

AtxImage::Place AtxImage::place_of_(SectorIndex sector) const noexcept {
    const SectorCount per_track = sectors_per_track_();
    return Place{
        .track = Track::of_image_sector(sector, per_track),
        .number = AtxSectorNumber::of_image_sector(sector, per_track)
    };
}

std::optional<AtxOffset> AtxImage::track_at_(Track track) const noexcept {
    if (!infra::affine::in_bounds(kTracks, track))
        return std::nullopt;
    const infra::OptRef<const std::optional<AtxOffset>> place = tracks_.find(track);
    if (!place)
        return std::nullopt;
    return *place;
}

SectorRead AtxImage::transfer_(const SectorAsk& ask, const Transfer& t) {
    const IAtxDriveModel& model = atx_drive_model(ask.drive);
    const AtxDrive& drive = model.drive();
    const bool writing = t.op == Op::Write;
    const bool verify = writing && ask.check == SectorCheck::Verify;
    Timeline clock{ask.at};
    int r = 1;

    const Place place = place_of_(ask.sector);
    const Track track = place.track;
    const AtxSectorNumber sector = place.number;
    FdcStatus status = FdcStatus::kMissing;
    const SectorSize size = sector_bytes_();

    clock.wait(seek_time(TrackMove{.from = head_track_, .to = track}, model));
    clock.stamp();
    head_track_ = track;

    std::array<std::byte, kTrackHeader.v> record{};
    AtxEntryCount count{};
    const std::optional<AtxOffset> track_at = track_at_(track);
    TrackHeader header{};
    bool header_read = false;
    if (track_at) {
        if (read_all(*file_, *track_at, record)) {
            header = TrackHeader::decode(record);
            count = header.sectors;
            header_read = true;
        } else {
            r = -1;
        }
    }
    if (!header_read || header.number != track || density_ != header.marked)
        count = AtxEntryCount{};

    const AtxOffset base = track_at.value_or(AtxOffset{});
    AtxOffset at = base;
    if (count != AtxEntryCount{}) {
        at = at + header.header_bytes;
        if (read_all(*file_, at, std::span{record}.first<kSectorList.v>())) {
            Fields list{record};
            const AtxLength next{list.take<std::uint32_t>()};
            at = at + (next - count.span(kSectorHeader));
        } else {
            count = AtxEntryCount{};
            r = -1;
        }
    }

    AtxLength target{};
    AtxOffset status_place{};
    WeakOffset weak{};
    FdcStatus write_status = FdcStatus::kMissing;
    SectorSize long_size{};
    AtxLength twin{};
    std::array<std::byte, kWidest.size()> verify_buf{};
    const AtxOffset retry_at = at;

    for (const AtxAttempt attempt : model.attempts()) {
        at = retry_at;
        AtxUnits previous{};
        AtxEntry target_entry{};
        target = AtxLength{};
        status_place = AtxOffset{};
        twin = AtxLength{};
        weak = WeakOffset{};
        write_status = FdcStatus::kMissing;

        for (AtxEntry i{}; infra::affine::in_bounds(count, i); i += AtxEntryCount{1}) {
            if (!read_all(*file_, at, std::span{record}.first<kSectorHeader.v>())) {
                r = -1;
                break;
            }
            const SectorHeader sh = SectorHeader::decode(record);
            if (writing
                && track == kTwinTrack
                && sh.number == kTwinNumber
                && sector == kTwinSector
                && sh.time == kTwinTime)
                twin = sh.data.value_or(AtxLength{});
            if (sh.number == sector) {
                if (sh.status.has(FdcStatus::kMissing)) {
                    write_status = write_status.with(sh.status);
                    at = at + kSectorHeader;
                    continue;
                }
                const AtxUnits tt = sh.time - clock.angle();
                if (target == AtxLength{}
                    || (tt > AtxUnits{} && previous <= AtxUnits{})
                    || (tt > AtxUnits{} && previous > AtxUnits{} && tt < previous)
                    || (tt <= AtxUnits{} && previous <= AtxUnits{} && tt < previous)) {
                    previous = tt;
                    status = sh.status;
                    status_place = at + AtxLength{1};
                    target_entry = i;
                    target = sh.data.value_or(AtxLength{});
                    if (twin != AtxLength{}
                        && track == kTwinTrack
                        && sector == kTwinSector
                        && sh.time != kTwinKeepTime)
                        twin = AtxLength{};
                }
            }
            at = at + kSectorHeader;
        }

        SectorSize act_size = size;
        long_size = SectorSize{};
        if (status.has(FdcStatus::kExtended)) {
            at = base + header.header_bytes;
            Chunk chunk{};
            constexpr unsigned kMostChunks = 4096;
            unsigned walked = 0;
            do {
                if (!read_all(*file_, at, std::span{record}.first<kChunk.v>())) {
                    r = -1;
                    break;
                }
                chunk = Chunk::decode(record);
                if (chunk.size != AtxLength{}) {
                    if (chunk.sector == target_entry) {
                        if (chunk.kind == AtxChunkKind::Weak) {
                            weak = chunk.data.weak();
                        } else if (chunk.kind == AtxChunkKind::Long) {
                            long_size = chunk.data.long_size();
                            act_size = model.after_long(act_size, chunk.data);
                        }
                    }
                    at = at + chunk.size;
                }
            } while (chunk.size != AtxLength{} && ++walked < kMostChunks);
        }

        if (target != AtxLength{}) {
            const AtxOffset data_at = base + target;
            const bool moved = writing ? write_all(*file_, data_at, size.of(t.from))
                                       : read_all(*file_, data_at, size.of(t.into));
            if (!moved) {
                r = -1;
                target = AtxLength{};
            } else {
                if (twin != AtxLength{}
                    && !write_all(
                        *file_,
                        base + twin + kTwinSkip,
                        infra::affine::first(
                            infra::affine::from(t.from, kTwinSkip),
                            size.bytes() - kTwinSkip.bytes()
                        )
                    )) {
                    r = -1;
                    target = AtxLength{};
                }
                if (target != AtxLength{} && verify) {
                    if (!read_all(*file_, data_at, size.of(std::span<std::byte>{verify_buf}))) {
                        r = -1;
                        target = AtxLength{};
                    } else if (
                        !std::ranges::equal(
                            size.of(t.from),
                            size.of(std::span<std::byte>{verify_buf})
                        )
                        || weak.random_from()
                    ) {
                        target = AtxLength{};
                    }
                }
            }
            const AtxUnits units = read_units(act_size, density_);
            const AtxUnits turn = previous > AtxUnits{} ? AtxUnits{} : kFullRotation;
            clock.wait_from_stamp(units + previous + turn);
            if (!status.clear())
                clock.wait(kFullRotation);
        } else {
            clock.wait_from_stamp(drive.lost);
            if (attempt == AtxAttempt::Reposition)
                clock.wait(model.reposition(track));
        }
        clock.stamp();
        if (status.clear() || r < 0)
            break;
    }

    const FdcStatus dropped = FdcStatus::kReserved.with(FdcStatus::kExtended);
    status = status.without(dropped);
    if (writing) {
        if (weak.none()) {
            if (target != AtxLength{}) {
                status = status.without(FdcStatus::kCrc.with(FdcStatus::kRecordType));
            } else {
                status = write_status.without(dropped);
            }
        }
    } else {
        status = model.read_status(status);
    }

    if (target != AtxLength{} && status.clear() && r >= 0)
        r = 0;

    if (!writing) {
        if (const std::optional<infra::ByteOffset> from = weak.random_from()) {
            for (std::byte& b :
                 infra::affine::from_clamped(infra::affine::first(t.into, size.bytes()), *from))
                b = noise_.next();
        }
        clock.wait_from_stamp(drive.checksum);
    } else if (target != AtxLength{} && weak.none()) {
        if (status_place != AtxOffset{}) {
            const std::array<std::byte, 1> back = {status.file_byte()};
            if (!write_all(*file_, status_place, back)) {
                r = -1;
                long_size = SectorSize{};
            }
        }
        infra::ByteCount pad_left = long_size > size ? long_size.bytes() - size.bytes()
                                                     : infra::ByteCount{};
        if (status.has(FdcStatus::kLost) && pad_left != infra::ByteCount{}) {
            std::array<std::byte, kPadBytes.size()> pad{};
            std::ranges::fill(pad, kPad);
            AtxOffset pad_at = base + target + AtxLength::of(size.bytes());
            while (pad_left != infra::ByteCount{}) {
                if (!write_all(*file_, pad_at, pad)) {
                    r = -1;
                    break;
                }
                pad_at = pad_at + AtxLength::of(kPadBytes);
                pad_left -= std::min(pad_left, kPadBytes);
            }
        }
    }

    return SectorRead{
        .bytes = size.bytes(),
        .status = status.wire(),
        .outcome = r == 0 ? SectorOutcome::Good : SectorOutcome::Faulty,
        .busy = clock.busy()
    };
}

}
