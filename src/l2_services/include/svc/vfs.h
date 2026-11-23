// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/affine.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/archive.h"
#include "svc/asset_name.h"
#include "svc/asset_query.h"
#include "svc/dir_entry.h"
#include "svc/durable_write.h"
#include "svc/file.h"
#include "svc/move_names.h"
#include "svc/rename_files.h"
#include "svc/scan_filter.h"
#include "svc/search_policy.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

class MemoryMounts;
class CardWriter;

using proto::FileSize;

struct UsbSlot {
    std::uint8_t v = 0;

    [[nodiscard]] std::string path() const;
    friend constexpr bool operator==(UsbSlot, UsbSlot) = default;
};

enum class RootChoice { Sd, Usb };

struct StorageRoot {
    std::optional<UsbSlot> usb;

    [[nodiscard]] bool on_usb() const noexcept { return usb.has_value(); }
};

class Vfs {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    ~Vfs();
    Vfs(Vfs&&) noexcept;
    Vfs& operator=(Vfs&&) noexcept;
    Vfs(const Vfs&) = delete;
    Vfs& operator=(const Vfs&) = delete;

    [[nodiscard]] static Ex<Vfs> create(RootChoice choice);

    [[nodiscard]] static Ex<Vfs> create_at(std::string_view root_path);

    const std::string& root_path() const noexcept { return root_path_; }

    const StorageRoot& root() const noexcept { return root_; }

    [[nodiscard]] Ex<std::string> resolve(std::string_view rel, const SearchPolicy& policy) const;

    Ex<std::unique_ptr<IFile>> open(std::string_view path, OpenMode mode) const;

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_zip_by_crc(
        std::string_view path,
        Crc32 crc
    ) const;

    [[nodiscard]] Ex<std::unique_ptr<IArchive>> open_archive(
        std::string_view path,
        std::size_t max_entries
    ) const;

    Ex<std::vector<DirEntry>> scan(std::string_view dir, const ScanFilter& f) const;

    [[nodiscard]] Ex<std::vector<DirEntry>> list_names(std::string_view dir) const;
    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_named(std::string_view path, OpenMode mode) const;

    bool dir_exists(std::string_view path) const noexcept;
    bool file_exists(std::string_view path) const noexcept;

    enum class NameKind : std::uint8_t { Absent, PlainFile, Other };
    [[nodiscard]] NameKind name_kind(std::string_view path) const noexcept;

    [[nodiscard]] Ex<std::string> canonical(std::string_view path) const;

    [[nodiscard]] Ex<std::chrono::sys_seconds> modified(std::string_view path) const;

    struct Space {
        FileSize total{};
        FileSize available{};

        [[nodiscard]] constexpr FileSize used() const noexcept {
            return infra::affine::saturating_sub(total, available);
        }
    };

    [[nodiscard]] Ex<Space> space(std::string_view path) const;

    using DurabilityLog = DurableWrite::DurabilityLog;
    static DurabilityLog durability_log() noexcept;

    static bool extension_matches(std::string_view filename, std::string_view packed_extensions) {
        return asset_name::extension_matches(filename, packed_extensions);
    }

    static std::optional<Crc32> parse_crc_name(std::string_view filename, std::string_view ext) {
        return asset_name::parse_crc_name(filename, ext);
    }

    using AssetQuery = svc::AssetQuery;
    [[nodiscard]] Ex<std::string> find_game_asset(const AssetQuery& q) const;

private:
    friend struct VfsTestAccess;
    friend class CardWriter;

    Vfs(StorageRoot root, std::string root_path);

    Ex<std::unique_ptr<IFile>> open_backend_(std::string_view path, OpenMode mode) const;

    Ex<std::unique_ptr<IFile>> open_named_(std::string_view path, OpenMode mode) const;
    [[nodiscard]] std::optional<Ex<std::unique_ptr<IFile>>> open_memory_(
        const std::string& full,
        OpenMode mode
    ) const;
    Ex<std::unique_ptr<IFile>> open_file_(const std::string& full, OpenMode mode) const;
    [[nodiscard]] std::string composed_(std::string_view path) const;

    Ex<std::unique_ptr<IFile>> open_write_(std::string_view path, OpenMode mode) const;

    [[nodiscard]] Ex<void> ensure_dir_(std::string_view rel) const;

    [[nodiscard]] Ex<void> replace_(const RenameFiles& names) const;

    void sync_parent_dir_(std::string_view path) const noexcept;

    [[nodiscard]] Ex<void> remove_(std::string_view rel) const;
    [[nodiscard]] Ex<void> remove_dir_(std::string_view rel) const;
    [[nodiscard]] Ex<void> move_(const MoveNames& names) const;

    StorageRoot root_{};
    std::string root_path_;
    std::unique_ptr<MemoryMounts> memory_;
    DurableWrite durable_;
};

}
