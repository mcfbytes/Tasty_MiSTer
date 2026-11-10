// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {

class DurableWrite {

public:
    explicit DurableWrite(std::string_view root_path) : root_path_(root_path) {}

    [[nodiscard]] Ex<void> replace(std::string_view tmp_rel, std::string_view final_rel) const;

    void sync_parent_dir(std::string_view path) const noexcept;

    void sync_dir_containing(const std::string& abs) const noexcept { sync_dir_of_(abs); }

    struct DurabilityLog {
        std::uint64_t file_syncs = 0;
        std::uint64_t dir_syncs = 0;
        std::uint64_t renames = 0;
        std::uint64_t last_file_sync = 0;
        std::uint64_t last_rename = 0;
    };

    [[nodiscard]] static DurabilityLog log() noexcept;

    static void note_file_sync() noexcept;

private:
    void sync_dir_of_(const std::string& path) const noexcept;

    std::string root_path_;
};

}
