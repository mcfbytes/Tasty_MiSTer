// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "infra/error.h"
#include "svc/file.h"

namespace mister::svc {

struct InflateResult {
    bool ok = false;
    std::vector<std::uint8_t> data;
};

[[nodiscard]] Ex<InflateResult> inflate_gzip(IFile& src, std::size_t cap);

}  // namespace mister::svc
