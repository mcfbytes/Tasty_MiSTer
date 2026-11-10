// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/vfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include "browse_rules.h"
#include "infra/unique_fd.h"
#include "os/block_device.h"
#include "posix_file.h"
#include "svc/game_asset_finder.h"
#include "vfs_detail.h"
#include "vfs_stat.h"
#include "vfs_zip.h"

static_assert(sizeof(::off_t) >= 8, "_FILE_OFFSET_BITS=64 is required (warnings.cmake)");

namespace mister::svc {

using detail::compose;
using detail::path_exists;
using detail::path_is_dir;
using detail::path_is_regular;
using detail::zip_archive_opens;
using detail::zip_list;
using detail::zip_make_archive;
using detail::zip_open_member;
using detail::zip_open_member_by_crc;

using infra::ieq;
using infra::istarts_with;

namespace {

constexpr std::string_view kSdRoot = "/media/fat";
constexpr std::string_view kMediaDir = "/media";
constexpr std::string_view kNetworkDir = "/media/network";
constexpr std::string_view kCifsDir = "cifs";
constexpr std::string_view kGamesDir = "games";
constexpr std::string_view kConfigDir = "config";

constexpr std::uint8_t kUsbRootSlots = 4;
constexpr std::uint8_t kUsbSearchSlots = 6;

constexpr unsigned long kExt4SuperMagic = 0xEF53UL;

bool usb_mounted(UsbSlot slot) {
    const std::string path = slot.path();

    struct ::stat self{};
    if (::stat(path.c_str(), &self) != 0)
        return false;
    if (!S_ISDIR(self.st_mode))
        return false;

    struct ::stat parent{};
    if (::stat(std::string(kMediaDir).c_str(), &parent) != 0)
        return false;
    if (self.st_dev == parent.st_dev)
        return false;

    struct ::statfs fs{};
    if (::statfs(path.c_str(), &fs) != 0)
        return false;
    return static_cast<unsigned long>(fs.f_type) != kExt4SuperMagic;
}

std::optional<std::string> fold_case_lookup(std::string_view base, std::string_view rel) {
    std::string cur(base);
    while (cur.size() > 1 && cur.back() == '/')
        cur.pop_back();

    std::size_t i = 0;
    while (i < rel.size()) {
        std::size_t j = rel.find('/', i);
        if (j == std::string_view::npos)
            j = rel.size();
        const std::string_view comp = rel.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == ".")
            continue;

        std::string exact = cur;
        exact.push_back('/');
        exact.append(comp);
        if (path_exists(exact)) {
            cur = std::move(exact);
            continue;
        }

        ::DIR* d = ::opendir(cur.c_str());
        if (d == nullptr)
            return std::nullopt;
        std::string hit;
        while (const ::dirent* de = ::readdir(d)) {
            if (ieq(std::string_view(de->d_name), comp)) {
                hit = de->d_name;
                break;
            }
        }
        ::closedir(d);
        if (hit.empty())
            return std::nullopt;
        cur.push_back('/');
        cur.append(hit);
    }
    return cur;
}

}

namespace {

int open_flags(OpenMode m) {
    switch (m) {
        case OpenMode::Read:
        case OpenMode::ReadWhole:
            return O_RDONLY | O_CLOEXEC;
        case OpenMode::ReadWrite:
            return O_RDWR | O_CLOEXEC;
        case OpenMode::Create:
            return O_RDWR | O_CREAT | O_CLOEXEC;
        case OpenMode::Truncate:
            return O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC;
        case OpenMode::Sync:
            return O_RDWR | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC;
    }
    return O_RDONLY | O_CLOEXEC;
}

Ex<std::unique_ptr<IFile>> hinted_whole(Ex<std::unique_ptr<IFile>> f) {
    if (f)
        (void)(*f)->advise(IFile::Access::Sequential, 0, 0);
    return f;
}

bool mode_creates(OpenMode m) {
    return m == OpenMode::Create || m == OpenMode::Truncate || m == OpenMode::Sync;
}

constexpr ::mode_t kCreateMode = S_IRWXU | S_IRWXG | S_IRWXO;

}

std::string UsbSlot::path() const {
    std::string s = "/media/usb";
    s.push_back(static_cast<char>('0' + v));
    return s;
}

Ex<Vfs> Vfs::create(RootChoice choice) {
    if (choice == RootChoice::Sd)
        return Vfs{StorageRoot{}, std::string(kSdRoot)};

    for (std::uint8_t i = 0; i < kUsbRootSlots; ++i) {
        const UsbSlot slot{i};
        if (usb_mounted(slot))
            return Vfs{StorageRoot{.usb = slot}, slot.path()};
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<Vfs> Vfs::create_at(std::string_view root_path) {
    if (root_path.empty() || root_path.front() != '/') {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EINVAL)});
    }
    std::string path(root_path);
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    if (!path_is_dir(path)) {
        return std::unexpected(
            Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(ENOENT)}
        );
    }
    return Vfs{StorageRoot{}, std::move(path)};
}

Ex<std::string> Vfs::resolve(std::string_view rel, const SearchPolicy& policy) const {
    if (!rel.empty() && rel.front() == '/') {
        return std::string(rel);
    }
    if (policy.order.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    const auto probe = [&](std::string_view base) -> std::optional<std::string> {
        std::string cand = compose(base, rel);
        if (path_exists(cand))
            return cand;
        if (!policy.case_insensitive)
            return std::nullopt;
        if (auto folded = fold_case_lookup(base, rel))
            return folded;
        return std::nullopt;
    };

    for (const SearchDir d : policy.order) {
        switch (d) {
            case SearchDir::CurrentRoot:
                if (auto p = probe(root_path_))
                    return *p;
                break;
            case SearchDir::SdRoot:

                if (auto p = probe(kSdRoot))
                    return *p;
                break;
            case SearchDir::UsbPrefixed:

                for (std::uint8_t i = 0; i < kUsbSearchSlots; ++i) {
                    const std::string usb_dir = UsbSlot{i}.path();
                    if (auto p = probe(usb_dir))
                        return *p;
                }
                break;
            case SearchDir::Network:
                if (auto p = probe(kNetworkDir))
                    return *p;
                break;
            case SearchDir::Cifs:
                if (auto p = probe(compose(root_path_, kCifsDir)))
                    return *p;
                break;
            case SearchDir::CoreDir:

                if (auto p = probe(compose(root_path_, kGamesDir)))
                    return *p;
                break;
            case SearchDir::ConfigDir:
                if (auto p = probe(compose(root_path_, kConfigDir)))
                    return *p;
                break;
        }
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<std::unique_ptr<IFile>> Vfs::open(std::string_view path, OpenMode mode) const {
    auto f = open_backend_(path, mode);
    if (mode == OpenMode::ReadWhole)
        return hinted_whole(std::move(f));
    return f;
}

Ex<std::unique_ptr<IFile>> Vfs::open_backend_(std::string_view path, OpenMode mode) const {
    const std::string full = compose(root_path_, path);

    if (memory_.mounted(full)) {
        if (!opens_read_only(mode)) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
        }
        return memory_.open(full);
    }

    const asset_name::ZipSplit zs = asset_name::zip_split(full);
    if (zs.zipped) {

        if (!opens_read_only(mode)) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
        }
        if (zs.member.empty()) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EISDIR)});
        }
        if (detail::has_nested_zip(full)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        return zip_open_member(std::string(zs.archive), std::string(zs.member));
    }

    const int flags = open_flags(mode);
    const int raw = ::open(full.c_str(), flags, kCreateMode);
    if (raw < 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    UniqueFd fd{raw};

    struct ::stat st{};
    if (::fstat(fd.get(), &st) != 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }

    detail::FileKind kind = detail::FileKind::Real;
    std::optional<std::uint64_t> fixed;

    if (istarts_with(full, "/dev/shm/")) {

        kind = detail::FileKind::Shm;
    } else if (S_ISBLK(st.st_mode) || (st.st_rdev != 0 && st.st_size == 0)) {

        kind = detail::FileKind::BlockDev;
        const Ex<std::uint64_t> bytes = os::BlockDevice::borrow(fd.get()).size_bytes();
        if (!bytes)
            return std::unexpected(bytes.error());
        fixed = *bytes;
    }

    const bool unlink_at_close = (kind == detail::FileKind::Shm) && mode_creates(mode);
    return std::unique_ptr<IFile>(
        new detail::PosixFile(static_cast<UniqueFd&&>(fd), kind, fixed, full, unlink_at_close)
    );
}

Ex<void> Vfs::mount_memory(std::string_view path, std::span<const std::byte> blob, Crc32 crc) {
    if (path.empty()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EINVAL)});
    }
    memory_.mount(compose(root_path_, path), blob, crc);
    return {};
}

std::uint32_t Vfs::memory_sequential_hints(std::string_view path) const noexcept {
    return memory_.sequential_hints(compose(root_path_, path));
}

Ex<std::unique_ptr<IFile>> Vfs::open_zip_by_crc(std::string_view path, Crc32 crc) const {
    if (crc.declared()) {
        const std::string full = compose(root_path_, path);
        const asset_name::ZipSplit zs = asset_name::zip_split(full);
        if (zs.zipped && !zs.member.empty() && !detail::has_nested_zip(full)) {

            const std::string prefix = std::string(zs.archive) + "/";
            if (auto hit = memory_.open_by_crc_under(prefix, crc))
                return hit;
            if (auto hit = zip_open_member_by_crc(std::string(zs.archive), crc)) {
                return hit;
            }
        }
    }
    return open(path, OpenMode::Read);
}

Ex<std::unique_ptr<IArchive>> Vfs::open_archive(
    std::string_view path,
    std::size_t max_entries
) const {
    const std::string full = compose(root_path_, path);
    return zip_make_archive(full, max_entries);
}

bool Vfs::dir_exists(std::string_view path) const noexcept {
    return path_is_dir(compose(root_path_, path));
}

bool Vfs::file_exists(std::string_view path) const noexcept {
    return path_is_regular(compose(root_path_, path));
}

Ex<std::chrono::sys_seconds> Vfs::modified(std::string_view path) const {
    const std::string full = compose(root_path_, path);
    struct ::stat st{};
    if (::stat(full.c_str(), &st) != 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    return std::chrono::sys_seconds{std::chrono::seconds{st.st_mtim.tv_sec}};
}

Ex<std::vector<DirEntry>> Vfs::scan(std::string_view dir, const ScanFilter& f) const {
    const std::string full = compose(root_path_, dir);

    if (detail::has_nested_zip(full)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const asset_name::ZipSplit zs = asset_name::zip_split(full);
    if (zs.zipped) {
        auto listing = zip_list(std::string(zs.archive));
        if (!listing)
            return std::unexpected(listing.error());
        return detail::zip_folder_entries(zs.member, listing->members, f);
    }

    ::DIR* d = ::opendir(full.c_str());
    if (d == nullptr) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }

    std::vector<DirEntry> out;
    const detail::BrowseDepth depth = dir.empty() ? detail::BrowseDepth::Root
                                                  : detail::BrowseDepth::Nested;

    for (;;) {
        errno = 0;
        const ::dirent* de = ::readdir(d);
        if (de == nullptr) {
            const int e = errno;
            if (e != 0) {
                ::closedir(d);
                return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
            }
            break;
        }
        const std::string_view name(de->d_name);
        if (name == ".")
            continue;

        std::optional<detail::EntryKind> kind;
        bool have_st = false;
        struct ::stat st{};
        if (de->d_type == DT_DIR) {
            kind = detail::EntryKind::Dir;
        } else if (de->d_type == DT_REG) {
            kind = detail::EntryKind::File;
        }
        if (de->d_type == DT_LNK || de->d_type == DT_REG || de->d_type == DT_UNKNOWN) {
            std::string child = full;
            child.push_back('/');
            child.append(name);
            if (::stat(child.c_str(), &st) == 0) {
                have_st = true;
                if (S_ISDIR(st.st_mode)) {
                    kind = detail::EntryKind::Dir;
                } else if (S_ISREG(st.st_mode)) {
                    kind = detail::EntryKind::File;
                }
            }
        }
        if (!kind)
            continue;

        const detail::Admit verdict = detail::admit_entry(name, *kind, depth, f);
        if (verdict == detail::Admit::Reject)
            continue;

        DirEntry e;
        e.name = std::string(name);
        e.is_dir = (verdict == detail::Admit::KeepDir);
        e.is_zip_member = false;

        if (!e.is_dir && have_st) {
            e.size = FileSize{static_cast<std::uint64_t>(st.st_size)};
        }
        out.push_back(std::move(e));
    }
    ::closedir(d);

    std::stable_sort(out.begin(), out.end(), detail::dirent_less);
    return out;
}

Ex<void> Vfs::ensure_dir(std::string_view rel) const {
    const std::string full = compose(root_path_, rel);

    std::string cur;
    if (!full.empty() && full.front() == '/')
        cur.push_back('/');

    std::size_t i = cur.empty() ? 0 : 1;
    while (i < full.size()) {
        std::size_t j = full.find('/', i);
        if (j == std::string::npos)
            j = full.size();
        const std::string_view comp(full.data() + i, j - i);
        i = j + 1;
        if (comp.empty())
            continue;

        if (!cur.empty() && cur.back() != '/')
            cur.push_back('/');
        cur.append(comp);

        if (::mkdir(cur.c_str(), kCreateMode) == 0) {

            durable_.sync_dir_containing(cur);
        } else if (errno != EEXIST) {
            const auto e = static_cast<std::uint32_t>(errno);
            return std::unexpected(Error{Errc::os, ERR_SITE(), e});
        }
    }
    return {};
}

Vfs::DurabilityLog Vfs::durability_log() noexcept {
    return DurableWrite::log();
}

Ex<std::string> Vfs::find_game_asset(const AssetQuery& q) const {
    return GameAssetFinder{root_path_}.find(q);
}

}
