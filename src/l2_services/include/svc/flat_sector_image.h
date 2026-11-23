// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/sector_image.h"

namespace mister::svc {

class FlatSectorImage final : public ISectorImage {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    [[nodiscard]] static Ex<std::unique_ptr<ISectorImage>> open(
        std::unique_ptr<IFile> file,
        bool writable,
        SectorSize headerless
    );

    [[nodiscard]] SectorGeometry geometry() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<SectorRead> read_sector(
        const SectorAsk& ask,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<SectorRead> write_sector(
        const SectorAsk& ask,
        std::span<const std::byte> src
    ) override;
    [[nodiscard]] Ex<void> clear() override;

    static constexpr proto::FileSize kAtrHeaderBytes{16};
    static constexpr std::byte kAtrMagic[] = {std::byte{0x96}, std::byte{0x02}};

private:
    FlatSectorImage(
        std::unique_ptr<IFile> file,
        bool writable,
        proto::FileOffset first,
        SectorGeometry geometry
    ) noexcept
        : file_(std::move(file)),
          writable_(writable),
          first_(first),
          geometry_(geometry) {}

    [[nodiscard]] proto::FileOffset place_of_(SectorIndex at) const noexcept;

    std::unique_ptr<IFile> file_;
    bool writable_;
    proto::FileOffset first_;
    SectorGeometry geometry_;
};

}
