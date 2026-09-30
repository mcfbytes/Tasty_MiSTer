// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/disk_format_row.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::svc {

class IDiskImage {
public:
    virtual ~IDiskImage() = default;

    [[nodiscard]] virtual FileSize size() const noexcept = 0;
    [[nodiscard]] virtual bool writable() const noexcept = 0;

    [[nodiscard]] virtual Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(std::uint64_t off,
                                                   std::span<const std::byte> src) = 0;

protected:
    IDiskImage() = default;
    IDiskImage(const IDiskImage&) = default;
    IDiskImage& operator=(const IDiskImage&) = default;
};

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> open_disk_image(const Vfs& vfs, std::string_view path,
                                                              std::span<const DiskFormatRow> rows,
                                                              OpenMode mode);

}  // namespace mister::svc
