// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "os/small_file.h"

namespace mister::os::detail {

[[nodiscard]] inline Ex<std::string_view> read_proc_text(
    const char* path,
    std::span<char> buf
) noexcept {
    const auto got = read_small_file(path, buf.first(buf.size() - 1));
    if (!got)
        return std::unexpected(got.error());
    buf[got->size()] = '\0';
    return std::string_view{buf.data(), got->size()};
}

}
