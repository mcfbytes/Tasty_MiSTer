// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/n64_db.h"
#include "cores/n64_db_tags.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

class N64DbReader {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::size_t kChunkBytes = 8192;

    static constexpr std::size_t kRelChars = 128;

    N64DbReader(const svc::Vfs& vfs, std::string_view core_dir) noexcept
        : vfs_(vfs),
          core_dir_(core_dir) {}

    [[nodiscard]] n64::DbTags scan(n64::DbKey kind, std::string_view key);

    [[nodiscard]] std::uint32_t long_lines() const noexcept { return long_lines_; }

private:
    [[nodiscard]] n64::DbTags scan_file_(
        std::string_view name,
        n64::DbKey kind,
        std::string_view key
    );

    const svc::Vfs& vfs_;
    std::string_view core_dir_;
    std::uint32_t long_lines_ = 0;
    char buf_[kChunkBytes]{};
};

}
