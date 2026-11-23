// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/file.h"

namespace mister::svc {

class T64Image final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IFile> file,
        FileSize size
    );

    [[nodiscard]] FileSize size() const noexcept override;

    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;

private:
    explicit T64Image(std::vector<std::byte> image) noexcept;
    std::vector<std::byte> image_;
};

}
