// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/sio_drive.h"

#include <utility>

namespace mister::cores::atari8 {

namespace {

constexpr svc::SectorStatus kDiskStatus{0x00};
}

void SioDrive::bind(
    std::unique_ptr<svc::ISectorImage> image,
    Medium medium,
    bool read_only
) noexcept {
    TASTY_SEAT_BODY(SioDrive);
    image_ = std::move(image);
    medium_ = image_ ? medium : Medium::Empty;
    own_ = medium_;
    read_only_ = read_only;
    partition_.reset();
    status_ = medium_ == Medium::Disk ? kDiskStatus : svc::kSectorClear;
    boot_ = {};
}

void SioDrive::map(const SioPartition& partition) noexcept {
    TASTY_SEAT_BODY(SioDrive);
    partition_ = partition;
    medium_ = Medium::Partition;
    status_ = svc::kSectorClear;
}

void SioDrive::unmap() noexcept {
    TASTY_SEAT_BODY(SioDrive);
    if (!partition_)
        return;
    partition_.reset();
    medium_ = own_;
    status_ = svc::kSectorClear;
}

void SioDrive::close() noexcept {
    TASTY_SEAT_BODY(SioDrive);
    bind(nullptr, Medium::Empty, false);
}

SioDrive::Medium SioDrive::medium() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return medium_;
}

bool SioDrive::read_only() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return partition_ ? partition_->read_only : read_only_;
}

svc::SectorSize SioDrive::sector_size() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    if (partition_)
        return partition_->size;
    return image_ ? image_->geometry().size : svc::SectorSize{};
}

svc::SectorCount SioDrive::sector_count() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    if (partition_)
        return partition_->count;
    return image_ ? image_->geometry().count : svc::SectorCount{};
}

bool SioDrive::spread() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return partition_ && partition_->size < kCardSector;
}

void SioDrive::gather_spread(std::span<std::byte> card) const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    const std::size_t step = kCardSector.bytes() / sector_size().bytes();
    for (std::size_t from = 0, to = 0; from < card.size(); from += step, ++to)
        card[to] = card[from];
}

void SioDrive::scatter_spread(
    std::span<const std::byte> src,
    std::span<std::byte> card
) const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    const std::size_t step = kCardSector.bytes() / sector_size().bytes();
    for (std::size_t from = 0, to = 0; to < card.size(); ++from, to += step)
        card[to] = src[from];
}

SioSectorOrigin SioDrive::sector_origin() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return medium_ == Medium::Disk ? SioSectorOrigin::FromZero : SioSectorOrigin::FromOne;
}

svc::SectorStatus SioDrive::status() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return status_;
}

void SioDrive::set_status(svc::SectorStatus status) noexcept {
    TASTY_SEAT_BODY(SioDrive);
    status_ = status;
}

infra::OptRef<svc::ISectorImage> SioDrive::image() noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return infra::opt_ref_of(image_.get());
}

const std::optional<SioPartition>& SioDrive::partition() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return partition_;
}

void SioDrive::set_boot(SioUnit boot) noexcept {
    TASTY_SEAT_BODY(SioDrive);
    boot_ = boot;
}

SioUnit SioDrive::boot_partition() const noexcept {
    TASTY_SEAT_BODY(SioDrive);
    return boot_;
}

}
