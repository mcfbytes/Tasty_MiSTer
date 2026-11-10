// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace mister::ui {

enum class EntryKind : std::uint8_t { File, Dir, UpDir, ZipDir, Recent };

struct BrowserEntry {
    std::string name;
    std::string altname;
    std::string datecode;

    std::string dir;
    EntryKind kind = EntryKind::File;

    bool stippled = false;

    [[nodiscard]] bool is_dir() const noexcept { return kind != EntryKind::File; }

    [[nodiscard]] bool is_up() const noexcept { return kind == EntryKind::UpDir; }

    [[nodiscard]] bool is_zip_dir() const noexcept { return kind == EntryKind::ZipDir; }

    [[nodiscard]] bool is_recent() const noexcept { return kind == EntryKind::Recent; }
};

}
