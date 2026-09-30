// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/board_profile.h"

#include <array>
#include <span>

#include "hal/boards_table.h"
#include "os/small_file.h"

namespace mister::hal {

[[nodiscard]] Ex<CompatibleBlob> read_compatible(const char* path) {

    std::array<char, CompatibleBlob::kCap + 1> buf{};
    const auto read = os::read_small_file(path, std::span<char>(buf));
    if (!read) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), read.error().detail});
    }
    const std::size_t total = *read;
    if (total == 0) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    }
    if (total == buf.size() || buf[total - 1] != '\0') {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    CompatibleBlob blob;
    if (const auto r = blob.assign(std::span<const char>(buf.data(), total)); !r) {
        return std::unexpected(r.error());
    }
    return blob;
}

[[nodiscard]] Ex<const BoardProfile*> select_board(const CompatibleBlob& blob) {
    for (const BoardProfile* p : kBoardTable) {
        for (const std::string_view s : p->compatible) {
            if (blob.contains(s)) return p;
        }
    }
    return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
}

}  // namespace mister::hal
