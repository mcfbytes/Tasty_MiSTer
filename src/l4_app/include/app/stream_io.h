// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

struct StreamFile {
    std::unique_ptr<svc::IFile> f;
    proto::FileSize size{};
};

[[nodiscard]] Ex<StreamFile> stream_open(
    const svc::Vfs& vfs,
    std::string_view rel,
    svc::OpenMode mode = svc::OpenMode::Read
);

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, proto::FileSize off, std::span<std::byte> dst);

[[nodiscard]] Ex<std::vector<std::uint8_t>> read_whole(svc::IFile& f, proto::FileSize size);

[[nodiscard]] Ex<std::vector<std::uint8_t>> read_file(
    const svc::Vfs& vfs,
    std::string_view rel,
    std::optional<proto::FileSize> cap = std::nullopt
);

}
