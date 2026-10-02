// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/archive.h"
#include "svc/dir_entry.h"
#include "svc/file.h"
#include "svc/scan_filter.h"
#include "svc/search_policy.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

using proto::FileSize;

struct StorageRoot {
    bool usb = false;
    std::uint8_t usb_index = 0;
};

class Vfs {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    static Ex<Vfs> create(StorageRoot root);

    static Ex<Vfs> create_at(std::string_view root_path);

    const std::string& root_path() const noexcept { return root_path_; }
    const StorageRoot& root() const noexcept { return root_; }

    Ex<std::string> resolve(std::string_view rel, const SearchPolicy& policy) const;

    Ex<std::unique_ptr<IFile>> open(std::string_view path, OpenMode mode) const;

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_zip_by_crc(std::string_view path,
                                                             Crc32 crc) const;

    [[nodiscard]] Ex<std::unique_ptr<IArchive>> open_archive(std::string_view path,
                                                             std::size_t max_entries) const;

    Ex<void> mount_memory(std::string_view path, std::span<const std::byte> blob, Crc32 crc = {});

    [[nodiscard]] std::uint32_t memory_sequential_hints(std::string_view path) const noexcept;

    Ex<std::vector<DirEntry>> scan(std::string_view dir, const ScanFilter& f) const;

    bool dir_exists(std::string_view path) const noexcept;
    bool file_exists(std::string_view path) const noexcept;

    Ex<void> ensure_dir(std::string_view rel) const;

    Ex<void> replace(std::string_view tmp_rel, std::string_view final_rel) const;

    void sync_parent_dir(std::string_view rel) const noexcept;

    struct DurabilityLog {
        std::uint64_t file_syncs = 0;
        std::uint64_t dir_syncs = 0;
        std::uint64_t renames = 0;
        std::uint64_t last_file_sync = 0;
        std::uint64_t last_rename = 0;
    };
    static DurabilityLog durability_log() noexcept;

    static bool extension_matches(std::string_view filename, std::string_view packed_extensions);

    static std::optional<Crc32> parse_crc_name(std::string_view filename, std::string_view ext);

    struct AssetQuery {
        std::string_view rom_path;
        Crc32 crc{};
        std::string_view ext;
        std::string_view core_dir;

        std::string_view cd_asset_dir;

        bool require_zip = false;
    };
    Ex<std::string> find_game_asset(const AssetQuery& q) const;

private:
    static void sync_dir_of_(const std::string& path) noexcept;
    Vfs() = default;

    Ex<std::unique_ptr<IFile>> open_backend_(std::string_view path, OpenMode mode) const;

    struct MemoryBlob {
        std::string path;
        std::span<const std::byte> blob;
        Crc32 crc{};
        std::shared_ptr<std::atomic<std::uint32_t>> hints;
    };

    StorageRoot root_{};
    std::string root_path_;
    std::vector<MemoryBlob> memory_;
};

}  // namespace mister::svc
