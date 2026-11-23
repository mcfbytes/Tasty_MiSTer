// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/sio_drives.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>

#include "cores/atari8/apt_table.h"
#include "cores/atari8/sio_blocks.h"
#include "cores/atari8/sio_formats.h"
#include "cores/atari8/xex_boot_image.h"
#include "infra/affine.h"
#include "infra/ascii.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/path_parts.h"
#include "infra/span_cut.h"
#include "svc/card_writer.h"
#include "svc/sector_image.h"
#include "svc/vfs.h"

namespace mister::cores::atari8 {

namespace {

using Medium = SioDrive::Medium;
using ::mister::infra::affine::operator/;

constexpr svc::SectorSize kNarrow{128};
constexpr svc::SectorSize kDouble{256};
constexpr std::byte kFormatMark{0xFF};

[[nodiscard]] svc::SectorDrive drive_model(Atari8Status status) noexcept {
    return status.atx_1050() ? svc::SectorDrive::Model1050 : svc::SectorDrive::Model810;
}

}

std::array<std::byte, 4> SioStatusFrame::bytes() const noexcept {
    return {drive, fdc, timeout, last};
}

SioStatusFrame SioStatusFrame::of(SioStatusFacts facts, std::span<const std::byte> host) noexcept {
    constexpr svc::SectorCount kStandard{720};
    constexpr svc::SectorSize kNarrowSize{128};
    std::byte drive = kDisk;
    if (!facts.disk) {
        drive = kMotor;
        if (facts.read_only)
            drive |= kLocked;
        if (facts.count != kStandard)
            drive |= kMedium;
        if (facts.size != kNarrowSize)
            drive |= kWide;
    }
    const std::byte timeout = facts.disk ? kDiskId : kFloppyId;
    const std::byte last = facts.disk && !host.empty() ? host[0] : kBlank;
    return SioStatusFrame{.drive = drive, .fdc = facts.fdc, .timeout = timeout, .last = last};
}

SioPercom SioPercom::of(
    svc::SectorCount total,
    svc::SectorSize size,
    bool standard_drive
) noexcept {

    constexpr std::uint32_t kTracks = 40;
    constexpr std::uint32_t kSides = 2;
    constexpr svc::SectorCount kDoubleSided{36};
    constexpr svc::SectorCount kMediumPerTrack = svc::SectorCount{1040} / kTracks;
    constexpr svc::SectorSize kNarrowSize{128};
    constexpr svc::SectorSize kDoubleSize{256};
    constexpr std::byte kTrackMark{40};
    SioPercom out{
        .tracks = std::byte{1},
        .step = kStepMark,
        .per_track = total,
        .sides = std::byte{},
        .density = std::byte{},
        .size = size,
        .online = kFlat
    };
    if (standard_drive) {
        out.tracks = kTrackMark;
        out.per_track = total / kTracks;
        if (out.per_track == kDoubleSided) {
            out.per_track = out.per_track / kSides;
            out.sides = std::byte{1};
        }
        out.density = size == kDoubleSize || out.per_track == kMediumPerTrack ? kDense
                                                                              : std::byte{};
    } else {
        out.density = size == kNarrowSize ? std::byte{} : kDense;
    }
    return out;
}

void SioPercom::encode(Bytes out) const noexcept {
    infra::affine::from(out, kTracksAt).front() = tracks;
    infra::affine::from(out, kStepAt).front() = step;
    const std::array<std::byte, 4> per_track_be = per_track.be_bytes();
    std::ranges::copy(
        infra::affine::from(std::span{per_track_be}, kPerTrackHighAt),
        infra::affine::from(out, kPerTrackAt).begin()
    );
    infra::affine::from(out, kSidesAt).front() = sides;
    infra::affine::from(out, kDensityAt).front() = density;
    std::ranges::copy(size.be_bytes(), infra::affine::from(out, kSizeAt).begin());
    infra::affine::from(out, kOnlineAt).front() = online;
    std::ranges::fill(infra::affine::from(out, kReservedAt), std::byte{});
}

void encode_device_info(
    std::span<std::byte> buffer,
    infra::ByteCount length,
    std::optional<SioPartition> partition,
    svc::SectorSize size,
    svc::SectorCount count,
    bool disk
) noexcept {
    std::ranges::fill(length.first_in(buffer), std::byte{});
    infra::affine::from(buffer, kInfoMarkAt).front() = std::byte{1};
    infra::affine::from(buffer, kInfoMark2At).front() = std::byte{1};
    std::ranges::copy(size.le_bytes(), infra::affine::from(buffer, kInfoSizeAt).begin());
    std::ranges::copy(count.le_bytes(), infra::affine::from(buffer, kInfoCountAt).begin());
    if (disk)
        return;
    const std::array<std::byte, 2> id = partition ? partition->id.wire_bytes()
                                                  : std::array<std::byte, 2>{};
    std::ranges::copy(id, infra::affine::from(buffer, kInfoIdAt).begin());
    const std::array<std::byte, 4> first = partition ? partition->first.le_bytes()
                                                     : std::array<std::byte, 4>{};
    std::ranges::copy(
        infra::affine::first(std::span{first}, kInfoFirstBytes),
        infra::affine::from(buffer, kInfoFirstAt).begin()
    );
}

void copy_host_text(
    std::span<std::byte> buffer,
    std::span<const std::byte> host,
    HostText at
) noexcept {
    const std::span<const std::byte> length = infra::affine::from_clamped(host, at.length_at);
    if (length.empty())
        return;
    const std::size_t n = std::min(
        {std::to_integer<std::size_t>(length.front()),
         infra::affine::from_clamped(host, at.from).size(),
         infra::affine::from_clamped(buffer, at.to).size()}
    );
    std::ranges::copy(
        infra::affine::first_clamped(
            infra::affine::from_clamped(host, at.from),
            infra::ByteCount{n}
        ),
        infra::affine::from_clamped(buffer, at.to).begin()
    );
}

SioUnit SioDrives::unit_of(SioDevice device) noexcept {
    TASTY_SEAT_BODY(SioDrives);
    return device.unit();
}

std::optional<SioUnit> SioDrives::unit_of(SioSlot slot) noexcept {
    TASTY_SEAT_BODY(SioDrives);
    switch (slot) {
        case SioSlot::D1:
        case SioSlot::BootD1:
            return SioUnit::kD1;
        case SioSlot::D2:
            return SioUnit::kD2;
        case SioSlot::D3:
            return SioUnit::kD3;
        case SioSlot::D4:
            return SioUnit::kD4;
        case SioSlot::HardDisk:
            return kHardDisk;
        case SioSlot::Xex:
            return std::nullopt;
    }
    return std::nullopt;
}

infra::OptRef<SioDrive> SioDrives::drive(SioUnit unit) noexcept {
    TASTY_SEAT_BODY(SioDrives);
    return drives_.find(unit);
}

infra::OptRef<const SioDrive> SioDrives::drive(SioUnit unit) const noexcept {
    TASTY_SEAT_BODY(SioDrives);
    return drives_.find(unit);
}

void SioDrives::close_all() noexcept {
    TASTY_SEAT_BODY(SioDrives);
    for (SioDrive& d : drives_)
        d.close();
}

void SioDrives::unmap_all_() noexcept {
    for (SioDrive& d : drives_)
        d.unmap();
}

void SioDrives::mount(
    SioUnit unit,
    const svc::Vfs& vfs,
    std::string_view path,
    Atari8Status status
) {
    TASTY_SEAT_BODY(SioDrives);
    const infra::OptRef<SioDrive> found = drives_.find(unit);
    if (!found)
        return;
    SioDrive& d = *found;
    const bool disk = unit == kHardDisk;
    if (!disk && d.medium() == Medium::Partition)
        return;
    if (path.empty()) {
        d.close();
        if (disk)
            unmap_all_();
        return;
    }
    const std::string_view ext = infra::extension_bare(path);
    const bool boot = infra::ieq(ext, "xex");
    const bool locked = boot || status.drives_read_only();

    std::unique_ptr<svc::ISectorImage> image;
    bool read_only = locked;
    if (boot) {
        auto file = vfs.open(path, svc::OpenMode::Read);
        if (file) {
            auto opened = XexBootImage::open(std::move(*file), infra::basename(path));
            if (opened)
                image = std::move(*opened);
        }
    } else {
        const auto rows = disk ? std::span<const svc::DiskFormatRow>{kSioHardDiskFormats}
                               : std::span<const svc::DiskFormatRow>{kSioDiskFormats};
        const svc::SectorSize flat = disk ? kCardSector : kNarrow;
        if (!locked) {
            svc::CardWriter card{vfs};
            auto opened = svc::open_sector_image(card, path, rows, flat);
            if (opened)
                image = std::move(*opened);
        }

        if (image == nullptr) {
            read_only = true;
            auto opened = svc::open_sector_image(vfs, path, rows, svc::OpenMode::Read, flat);
            if (opened)
                image = std::move(*opened);
        }
    }
    if (image == nullptr) {
        d.close();
        return;
    }
    const bool timed = !boot && infra::ieq(ext, "atx");
    const Medium medium = disk ? Medium::Disk
                               : (boot ? Medium::Boot : (timed ? Medium::Timed : Medium::Flat));
    d.bind(std::move(image), medium, read_only);
    if (disk && !scan_table_(read_only))
        d.close();
}

bool SioDrives::scan_table_(bool read_only) {
    SioDrive& hdd = drives_[kHardDisk];
    const infra::OptRef<svc::ISectorImage> image = hdd.image();
    if (!image)
        return false;
    const std::optional<AptTable> table = AptTable::find(*image);
    if (!table)
        return false;
    hdd.set_boot(table->boot());
    for (SioUnit unit{}; unit < kHardDisk; unit += SioDriveCount{1}) {
        SioDrive& d = drives_[unit];
        if (const std::optional<SioPartition> p = table->entry(unit, read_only))
            d.map(*p);
        else
            d.unmap();
    }
    return true;
}

bool SioDrives::serves_serial(const SioFrame& frame) const noexcept {
    TASTY_SEAT_BODY(SioDrives);
    if (!frame.device.serial_disk())
        return false;
    const infra::OptRef<const SioDrive> d = drives_.find(unit_of(frame.device));
    if (!d)
        return false;
    const Medium m = d->medium();
    const bool own = m == Medium::Flat || m == Medium::Timed || m == Medium::Boot;
    return own && d->sector_size() != kCardSector;
}

bool SioDrives::timed(SioUnit unit) const noexcept {
    TASTY_SEAT_BODY(SioDrives);
    const infra::OptRef<const SioDrive> d = drives_.find(unit);
    return d && d->medium() == Medium::Timed;
}

std::optional<SioCommand> SioDrives::verb_for(
    const SioFrame& frame,
    const SioCall& call
) const noexcept {
    TASTY_SEAT_BODY(SioDrives);
    const std::optional<SioCommand> command = frame.command();
    const infra::OptRef<const SioDrive> d = drives_.find(unit_of(frame.device));
    if (!command || !d)
        return std::nullopt;
    const bool pbi = frame.device.pbi();
    const bool writable = !d->read_only();
    const SioSectorOrigin origin = frame.device.sector_origin();
    const SioSector sector = frame.sector;
    const bool in_range = sector.index_in(origin, d->sector_count()).has_value();
    bool answered = false;
    switch (*command) {
        case SioCommand::Speed:
            answered = !pbi;
            break;
        case SioCommand::FormatSingle:
        case SioCommand::FormatMedium:
            answered = writable && !pbi;
            break;
        case SioCommand::MediaChange:
            answered = pbi;
            break;
        case SioCommand::ReadPercom:
            answered = origin == SioSectorOrigin::FromOne;
            break;
        case SioCommand::Status:
            answered = call.reads();
            break;
        case SioCommand::Write:
        case SioCommand::WriteVerify:
            answered = writable && call.writes() && in_range;
            break;
        case SioCommand::Read:
            answered = call.reads() && in_range;
            break;
        case SioCommand::DeviceInfo:
            answered = pbi && call.direction == SioDirection::FromDevice;
            break;
    }
    if (!answered)
        return std::nullopt;
    return command;
}

std::optional<SioDrives::Placed> SioDrives::place_(SioUnit unit, SioSector sector) noexcept {
    const infra::OptRef<SioDrive> found = drives_.find(unit);
    if (!found)
        return std::nullopt;
    SioDrive& d = *found;
    if (const std::optional<SioPartition>& p = d.partition()) {
        const infra::OptRef<svc::ISectorImage> hdd = drives_[kHardDisk].image();
        if (!hdd)
            return std::nullopt;
        return Placed{d, *hdd, p->first + sector.index_from(d.sector_origin())};
    }
    const infra::OptRef<svc::ISectorImage> own = d.image();
    if (!own)
        return std::nullopt;
    return Placed{d, *own, svc::SectorIndex{} + sector.index_from(d.sector_origin())};
}

infra::ByteCount SioDrives::write_bytes(const SioFrame& frame, SioUnit unit) const noexcept {
    TASTY_SEAT_BODY(SioDrives);
    const infra::OptRef<const SioDrive> d = drives_.find(unit);
    if (!d)
        return {};
    if (d->partition() || d->medium() == Medium::Disk || d->sector_size() == kCardSector)
        return kCardSector.bytes();
    return frame.sector.past_boot() ? d->sector_size().bytes() : kNarrow.bytes();
}

svc::SectorAsk SioDrives::ask_(svc::SectorIndex at, const SioCall& call) noexcept {
    return svc::SectorAsk{.sector = at, .at = call.now, .drive = drive_model(call.status)};
}

SioReply SioDrives::execute(
    SioCommand verb,
    const SioFrame& frame,
    SioUnit unit,
    const SioCall& call,
    Buffer buffer
) {
    TASTY_SEAT_BODY(SioDrives);
    switch (verb) {
        case SioCommand::Read:
            return read_(frame, unit, call, buffer);
        case SioCommand::Write:
        case SioCommand::WriteVerify:
            return write_(verb, frame, unit, call, buffer);
        case SioCommand::Status:
            return status_(unit, call, buffer);
        case SioCommand::ReadPercom:
            return percom_(unit, buffer);
        case SioCommand::FormatSingle:
        case SioCommand::FormatMedium:
            return format_(unit, buffer);
        case SioCommand::Speed:
            return speed_(frame, unit, call, buffer);
        case SioCommand::MediaChange:
            return media_change_();
        case SioCommand::DeviceInfo:
            return device_info_(unit, call, buffer);
    }
    return SioReply{.success = false};
}

SioReply SioDrives::read_(const SioFrame& frame, SioUnit unit, const SioCall& call, Buffer buffer) {
    const std::optional<Placed> placed = place_(unit, frame.sector);
    if (!placed)
        return SioReply{.success = false};
    SioDrive& d = placed->drive;
    if (d.partition()) {

        const std::span<std::byte> card = kCardSector.of(std::span<std::byte>{buffer});
        const auto got = placed->image.read_sector(ask_(placed->at, call), card);
        if (!got)
            return SioReply{.bytes = kCardSector.bytes(), .success = false};
        if (d.spread())
            d.gather_spread(card);
        return SioReply{.bytes = kCardSector.bytes()};
    }
    const auto got = placed->image.read_sector(ask_(placed->at, call), buffer);
    if (!got)
        return SioReply{.bytes = d.sector_size().bytes(), .success = false};
    if (d.medium() != Medium::Timed)
        return SioReply{.bytes = got->bytes};
    d.set_status(got->status);
    return SioReply{.bytes = got->bytes, .success = got->good(), .timed = got->busy};
}

SioReply SioDrives::write_(
    SioCommand verb,
    const SioFrame& frame,
    SioUnit unit,
    const SioCall& call,
    Buffer buffer
) {
    const std::optional<Placed> placed = place_(unit, frame.sector);
    if (!placed)
        return SioReply{.success = false, .respond = false};
    SioDrive& d = placed->drive;
    svc::SectorAsk ask = ask_(placed->at, call);
    ask.check = verb == SioCommand::WriteVerify ? svc::SectorCheck::Verify : svc::SectorCheck::None;
    SioReply reply{.bytes = d.sector_size().bytes(), .respond = false};
    if (d.spread()) {
        std::array<std::byte, kCardSector.v> card{};
        d.scatter_spread(buffer, card);
        const auto put = placed->image.write_sector(ask, card);
        reply.success = put && put->good();
        return reply;
    }
    const auto put = placed->image.write_sector(ask, buffer);
    reply.success = put && put->good();
    if (put && d.medium() == Medium::Timed) {
        d.set_status(put->status);
        reply.timed = put->busy;
    }
    return reply;
}

SioReply SioDrives::status_(SioUnit unit, const SioCall& call, Buffer buffer) const noexcept {
    const SioDrive& d = drives_[unit];
    const SioStatusFacts facts{
        .fdc = d.status().wire_byte(),
        .count = d.sector_count(),
        .size = d.sector_size(),
        .read_only = d.read_only(),
        .disk = unit == kHardDisk
    };
    const SioStatusFrame frame = SioStatusFrame::of(facts, call.host);
    const std::array<std::byte, 4> bytes = frame.bytes();
    std::ranges::copy(bytes, buffer.begin());
    return SioReply{.bytes = infra::ByteCount{bytes.size()}};
}

SioReply SioDrives::percom_(SioUnit unit, Buffer buffer) const noexcept {
    const SioDrive& d = drives_[unit];
    const bool standard = !d.partition()
        && unit != kHardDisk
        && SioPercom::standard(d.sector_count());
    const SioPercom block = SioPercom::of(d.sector_count(), d.sector_size(), standard);
    block.encode(buffer.first<SioPercom::kBytes.v>());
    return SioReply{.bytes = SioPercom::kBytes};
}

SioReply SioDrives::format_(SioUnit unit, Buffer buffer) {
    SioDrive& d = drives_[unit];
    const svc::SectorSize size = d.sector_size();
    std::ranges::fill(size.of(std::span<std::byte>{buffer}), std::byte{});
    const infra::OptRef<svc::ISectorImage> image = d.image();
    const bool cleared = image && image->clear().has_value();
    buffer[0] = kFormatMark;
    buffer[1] = kFormatMark;
    return SioReply{.bytes = size.bytes(), .success = cleared};
}

SioReply SioDrives::speed_(
    const SioFrame& frame,
    SioUnit unit,
    const SioCall& call,
    Buffer buffer
) const noexcept {

    const SioRate chosen = call.status.speed_choice().rate();
    SioRate answered = chosen;
    SioRate taken = frame.sector.standard_rate() ? SioSpeedChoice::kStandard : chosen;
    if (drives_[unit].medium() == Medium::Timed) {
        answered = SioSpeedChoice::kStandard;
        taken = SioSpeedChoice::kStandard;
    }
    buffer[0] = answered.wire_byte();
    return SioReply{.bytes = infra::ByteCount{1}, .divisor = taken.divisor()};
}

SioReply SioDrives::media_change_() {
    const bool scanned = drives_[kHardDisk].image().has_value() && scan_table_(false);
    return SioReply{.success = scanned, .respond = false};
}

SioReply SioDrives::device_info_(SioUnit unit, const SioCall& call, Buffer buffer) {
    SioDrive& d = drives_[unit];
    const std::size_t length = call.length.first_in(std::span<std::byte>{buffer}).size();
    const std::optional<SioPartition> p = d.partition();
    const bool disk = unit == kHardDisk;
    encode_device_info(
        buffer,
        infra::ByteCount{length},
        p,
        d.sector_size(),
        d.sector_count(),
        disk
    );
    SioReply reply{.bytes = infra::ByteCount{length}};
    if (disk) {
        copy_host_text(buffer, call.host, kHostName);
        copy_host_text(buffer, call.host, kHostLabel);
        return reply;
    }
    if (p && p->has_meta) {
        std::array<std::byte, kCardSector.v> card{};
        const infra::OptRef<svc::ISectorImage> hdd = drives_[kHardDisk].image();
        const bool read = hdd && hdd->read_sector(ask_(p->meta, call), card).has_value();
        if (read) {
            std::ranges::copy(
                infra::affine::first(
                    infra::affine::from(std::span{card}, kInfoMetaFrom),
                    kInfoMetaBytes
                ),
                infra::affine::from(buffer, kInfoName).begin()
            );
        } else {
            reply.success = false;
        }
    }
    return reply;
}

}
