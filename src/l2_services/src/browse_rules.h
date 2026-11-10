// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "svc/dir_entry.h"
#include "svc/scan_filter.h"

namespace mister::svc::detail {

enum class Admit : std::uint8_t {
    Reject,
    KeepFile,
    KeepDir,
};

enum class EntryKind : std::uint8_t { File, Dir };

enum class BrowseDepth : std::uint8_t { Root, Nested };

Admit admit_entry(std::string_view name, EntryKind kind, BrowseDepth depth, const ScanFilter& f);

inline constexpr std::string_view kExtDot = ".";

struct ZipMember {
    std::string name;
    proto::FileSize size{};
    bool is_dir = false;
};

std::vector<DirEntry> zip_folder_entries(
    std::string_view folder,
    std::span<const ZipMember> members,
    const ScanFilter& f
);

bool dirent_less(const DirEntry& a, const DirEntry& b) noexcept;

}
