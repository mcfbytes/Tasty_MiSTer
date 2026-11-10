// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "infra/error.h"

namespace mister::svc {

class IFile;
class Vfs;

[[nodiscard]] Ex<std::vector<std::byte>> read_bounded(IFile& file, std::uint64_t cap);

[[nodiscard]] std::span<std::byte> read_bounded_partial(IFile& file, std::span<std::byte> dst);

[[nodiscard]] Ex<std::vector<std::byte>> read_bounded(
    const Vfs& vfs,
    std::string_view path,
    std::uint64_t cap
);

}
