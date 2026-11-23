// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/atari8/sio_partition.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/sector_image.h"
#include "svc/sector_types.h"

namespace mister::cores::atari8 {

class SioDrive {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    enum class Medium : std::uint8_t { Empty, Flat, Timed, Boot, Disk, Partition };

    void bind(std::unique_ptr<svc::ISectorImage> image, Medium medium, bool read_only) noexcept;

    void map(const SioPartition& partition) noexcept;

    void unmap() noexcept;

    void close() noexcept;

    [[nodiscard]] Medium medium() const noexcept;
    [[nodiscard]] bool read_only() const noexcept;
    [[nodiscard]] svc::SectorSize sector_size() const noexcept;
    [[nodiscard]] svc::SectorCount sector_count() const noexcept;

    [[nodiscard]] bool spread() const noexcept;

    void gather_spread(std::span<std::byte> card) const noexcept;

    void scatter_spread(std::span<const std::byte> src, std::span<std::byte> card) const noexcept;

    [[nodiscard]] SioSectorOrigin sector_origin() const noexcept;
    [[nodiscard]] svc::SectorStatus status() const noexcept;
    void set_status(svc::SectorStatus status) noexcept;

    [[nodiscard]] infra::OptRef<svc::ISectorImage> image() noexcept;
    [[nodiscard]] const std::optional<SioPartition>& partition() const noexcept;

    void set_boot(SioUnit boot) noexcept;
    [[nodiscard]] SioUnit boot_partition() const noexcept;

private:
    std::unique_ptr<svc::ISectorImage> image_{};
    Medium medium_ = Medium::Empty;
    Medium own_ = Medium::Empty;
    bool read_only_ = false;
    std::optional<SioPartition> partition_{};
    svc::SectorStatus status_ = svc::kSectorClear;
    SioUnit boot_{};
};

}
