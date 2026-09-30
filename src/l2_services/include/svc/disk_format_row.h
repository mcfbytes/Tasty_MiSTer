// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "proto/types.h"
#include "svc/disk_codec.h"

namespace mister::svc {

struct DiskFormatRow {

    std::string_view ext{};
    std::uint32_t size_bytes = 0;
    std::uint32_t tolerance = 0;
    std::span<const std::byte> magic{};
    DiskCodec codec = DiskCodec::Raw;
    std::string_view why{};
};

inline constexpr std::size_t kDiskMagicProbeBytes = 64;

[[nodiscard]] const DiskFormatRow* select_disk_format(std::span<const DiskFormatRow> rows,
                                                      std::string_view path, proto::FileSize size,
                                                      std::span<const std::byte> head) noexcept;

[[nodiscard]] std::string_view disk_path_extension(std::string_view path) noexcept;

}  // namespace mister::svc
