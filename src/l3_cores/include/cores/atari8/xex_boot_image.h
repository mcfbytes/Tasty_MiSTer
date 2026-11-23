// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "cores/atari8/sio_types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/sector_image.h"

namespace mister::cores::atari8 {

class XexBootImage final : public svc::ISectorImage {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    [[nodiscard]] static Ex<std::unique_ptr<svc::ISectorImage>> open(
        std::unique_ptr<svc::IFile> file,
        std::string_view name
    );

    [[nodiscard]] svc::SectorGeometry geometry() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<svc::SectorRead> read_sector(
        const svc::SectorAsk& ask,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<svc::SectorRead> write_sector(
        const svc::SectorAsk& ask,
        std::span<const std::byte> src
    ) override;
    [[nodiscard]] Ex<void> clear() override;

    static constexpr SioSector kVtocSector{0x168};
    static constexpr SioSector kDirectorySector{0x169};
    static constexpr SioSector kFirstFileSector{0x171};
    static constexpr svc::SectorCount kStructureSectors{0x173};
    static constexpr svc::SectorSize kSectorSize{128};

    static constexpr svc::SectorSize kLinkData{125};

private:
    XexBootImage(std::unique_ptr<svc::IFile> file, std::string name, proto::FileSize size) noexcept
        : file_(std::move(file)),
          name_(std::move(name)),
          size_(size) {}

    void vtoc_(std::span<std::byte> sector) const noexcept;
    void directory_(std::span<std::byte> sector) const noexcept;
    [[nodiscard]] Ex<void> file_sector_(SioSector number, std::span<std::byte> sector);

    std::unique_ptr<svc::IFile> file_;
    std::string name_;
    proto::FileSize size_;
};

}
