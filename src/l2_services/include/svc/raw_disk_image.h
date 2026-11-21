// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/disk_image.h"
#include "svc/file.h"

namespace mister::svc {

class RawDiskImage final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    RawDiskImage(std::unique_ptr<IFile> file, FileSize size, bool writable) noexcept
        : file_(std::move(file)),
          size_(size),
          writable_(writable) {}

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
    std::unique_ptr<IFile> file_;
    FileSize size_;
    bool writable_;
};

}
