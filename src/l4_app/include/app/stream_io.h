// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

struct StreamFile {
    std::unique_ptr<svc::IFile> f;
    std::uint64_t size = 0;
};

[[nodiscard]] Ex<StreamFile> stream_open(const svc::Vfs& vfs, std::string_view rel,
                                         svc::OpenMode mode = svc::OpenMode::Read);

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, std::uint64_t off, std::span<std::byte> dst);

[[nodiscard]] Ex<void> stream_write(svc::IFile& f, std::uint64_t off,
                                    std::span<const std::byte> src);

[[nodiscard]] Ex<void> stream_sync(svc::IFile& f);

}  // namespace mister::app
