// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/vfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

#include "infra/persist.h"
#include "infra/unique_fd.h"
#include "vfs_detail.h"

#if __has_include(<linux/fs.h>)
#include <linux/fs.h>
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

static_assert(sizeof(::off_t) >= 8, "_FILE_OFFSET_BITS=64 is required (warnings.cmake)");

#include <minizip/unzip.h>

namespace mister::svc {

using detail::compose;
using detail::iends_with;
using detail::ieq;
using detail::ifind;
using detail::istarts_with;
using detail::to_lower;
using detail::ZipSplit;

namespace {

std::atomic<std::uint64_t> g_dura_ticket TASTY_PERSIST(proc, vfs_dura_ticket){0};
std::atomic<std::uint64_t> g_dura_file_syncs TASTY_PERSIST(proc, vfs_dura_file_syncs){0};
std::atomic<std::uint64_t> g_dura_dir_syncs TASTY_PERSIST(proc, vfs_dura_dir_syncs){0};
std::atomic<std::uint64_t> g_dura_renames TASTY_PERSIST(proc, vfs_dura_renames){0};
std::atomic<std::uint64_t> g_dura_last_file_sync TASTY_PERSIST(proc, vfs_dura_last_file_sync){0};
std::atomic<std::uint64_t> g_dura_last_rename TASTY_PERSIST(proc, vfs_dura_last_rename){0};

std::uint64_t dura_ticket() noexcept {
    return g_dura_ticket.fetch_add(1, std::memory_order_relaxed) + 1;
}

constexpr std::string_view kSdRoot = "/media/fat";
constexpr std::string_view kMediaDir = "/media";
constexpr std::string_view kNetworkDir = "/media/network";
constexpr std::string_view kCifsDir = "cifs";
constexpr std::string_view kGamesDir = "games";
constexpr std::string_view kConfigDir = "config";

constexpr std::uint8_t kUsbRootSlots = 4;
constexpr std::uint8_t kUsbSearchSlots = 6;

constexpr unsigned long kExt4SuperMagic = 0xEF53UL;

std::string usb_path(std::uint8_t n) {
    std::string s = "/media/usb";
    s.push_back(static_cast<char>('0' + n));
    return s;
}

bool usb_mounted(std::uint8_t n) {
    const std::string path = usb_path(n);

    struct ::stat self {};
    if (::stat(path.c_str(), &self) != 0) return false;
    if (!S_ISDIR(self.st_mode)) return false;

    struct ::stat parent {};
    if (::stat(std::string(kMediaDir).c_str(), &parent) != 0) return false;
    if (self.st_dev == parent.st_dev) return false;

    struct ::statfs fs {};
    if (::statfs(path.c_str(), &fs) != 0) return false;
    return static_cast<unsigned long>(fs.f_type) != kExt4SuperMagic;
}

bool path_exists(const std::string& p) {
    struct ::stat st {};
    return ::stat(p.c_str(), &st) == 0;
}

bool path_is_dir(const std::string& p) {
    struct ::stat st {};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_is_regular(const std::string& p) {
    struct ::stat st {};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::optional<std::string> fold_case_lookup(std::string_view base, std::string_view rel) {
    std::string cur(base);
    while (cur.size() > 1 && cur.back() == '/')
        cur.pop_back();

    std::size_t i = 0;
    while (i < rel.size()) {
        std::size_t j = rel.find('/', i);
        if (j == std::string_view::npos) j = rel.size();
        const std::string_view comp = rel.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == ".") continue;

        std::string exact = cur;
        exact.push_back('/');
        exact.append(comp);
        if (path_exists(exact)) {
            cur = std::move(exact);
            continue;
        }

        ::DIR* d = ::opendir(cur.c_str());
        if (d == nullptr) return std::nullopt;
        std::string hit;
        while (const ::dirent* de = ::readdir(d)) {
            if (ieq(std::string_view(de->d_name), comp)) {
                hit = de->d_name;
                break;
            }
        }
        ::closedir(d);
        if (hit.empty()) return std::nullopt;
        cur.push_back('/');
        cur.append(hit);
    }
    return cur;
}

class PosixFile final : public IFile {
public:
    PosixFile(UniqueFd fd, FileKind kind, std::optional<std::uint64_t> fixed_size, std::string path,
              bool unlink_at_close)
        : fd_(static_cast<UniqueFd&&>(fd)), path_(std::move(path)), fixed_size_(fixed_size),
          kind_(kind), unlink_at_close_(unlink_at_close) {}

    ~PosixFile() override {

        if (unlink_at_close_ && !path_.empty()) ::unlink(path_.c_str());
    }

    PosixFile(const PosixFile&) = delete;
    PosixFile& operator=(const PosixFile&) = delete;

    Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        std::size_t done = 0;
        while (done < dst.size()) {
            const ::ssize_t n = ::pread(fd_.get(), dst.data() + done, dst.size() - done,
                                        static_cast<::off_t>(off + done));
            if (n < 0) {
                if (errno == EINTR) continue;
                const auto e = static_cast<std::uint32_t>(errno);
                return std::unexpected(Error{Errc::os, ERR_SITE(), e});
            }

            if (n == 0) break;
            done += static_cast<std::size_t>(n);
        }
        return done;
    }

    Ex<std::size_t> write_at(std::uint64_t off, std::span<const std::byte> src) override {
        std::size_t done = 0;
        while (done < src.size()) {
            const ::ssize_t n = ::pwrite(fd_.get(), src.data() + done, src.size() - done,
                                         static_cast<::off_t>(off + done));
            if (n < 0) {
                if (errno == EINTR) continue;
                const auto e = static_cast<std::uint32_t>(errno);
                return std::unexpected(Error{Errc::os, ERR_SITE(), e});
            }
            if (n == 0) {
                return std::unexpected(
                    Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(done)});
            }
            done += static_cast<std::size_t>(n);
        }
        return done;
    }

    Ex<FileSize> size() const override {

        if (fixed_size_) return FileSize{*fixed_size_};
        struct ::stat st {};
        if (::fstat(fd_.get(), &st) != 0) {
            const auto e = static_cast<std::uint32_t>(errno);
            return std::unexpected(Error{Errc::os, ERR_SITE(), e});
        }
        return FileSize{static_cast<std::uint64_t>(st.st_size)};
    }

    Ex<void> flush() override {

        g_dura_last_file_sync.store(dura_ticket(), std::memory_order_relaxed);
        g_dura_file_syncs.fetch_add(1, std::memory_order_relaxed);
        if (::fsync(fd_.get()) != 0) {
            const auto e = static_cast<std::uint32_t>(errno);

            if (e != EINVAL && e != EROFS) {
                return std::unexpected(Error{Errc::os, ERR_SITE(), e});
            }
        }
        return {};
    }

    FileKind kind() const noexcept override { return kind_; }

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t off, std::uint64_t len) override {
        const int advice = (a == Access::Sequential) ? POSIX_FADV_SEQUENTIAL : POSIX_FADV_WILLNEED;
        const int rc = ::posix_fadvise(fd_.get(), static_cast<::off_t>(off),
                                       static_cast<::off_t>(len), advice);
        if (rc != 0)
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(rc)});
        return {};
    }

private:
    UniqueFd fd_;
    std::string path_;
    std::optional<std::uint64_t> fixed_size_{};
    FileKind kind_ = FileKind::Real;
    bool unlink_at_close_ = false;

    static_assert(std::is_trivially_copyable_v<decltype(fixed_size_)>);
};

class MemoryFile final : public IFile {
public:
    MemoryFile(std::span<const std::byte> blob, std::shared_ptr<std::atomic<std::uint32_t>> hints)
        : blob_(blob), hints_(std::move(hints)) {}

    Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= blob_.size()) return std::size_t{0};
        const std::size_t start = static_cast<std::size_t>(off);
        const std::size_t n = std::min(blob_.size() - start, dst.size());
        std::memcpy(dst.data(), blob_.data() + start, n);
        return n;
    }

    Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    Ex<FileSize> size() const override { return FileSize{blob_.size()}; }
    Ex<void> flush() override { return {}; }
    FileKind kind() const noexcept override { return FileKind::Memory; }

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        if (a == Access::Sequential) hints_->fetch_add(1, std::memory_order_relaxed);
        return {};
    }

private:
    std::span<const std::byte> blob_;
    std::shared_ptr<std::atomic<std::uint32_t>> hints_;
};

constexpr int kZipCaseInsensitive = 2;

constexpr std::size_t kZipReadChunk = 1u << 20;

constexpr std::uint64_t kZipReserveCap = 1u << 20;

bool zip_name_is_dir(std::string_view n) noexcept {
    return !n.empty() && (n.back() == '/' || n.back() == '\\');
}

std::string zip_current_name(unzFile uf, const unz_file_info64& fi) {
    std::string nm(static_cast<std::size_t>(fi.size_filename), '\0');
    if (fi.size_filename != 0 && ::unzGetCurrentFileInfo64(uf, nullptr, nm.data(), fi.size_filename,
                                                           nullptr, 0, nullptr, 0) != UNZ_OK) {
        nm.clear();
    }
    return nm;
}

Ex<unzFile> zip_open_archive(const std::string& archive) {
    unzFile uf = ::unzOpen64(archive.c_str());
    if (uf == nullptr) {
        if (::access(archive.c_str(), R_OK) != 0) {
            const auto e = static_cast<std::uint32_t>(errno);
            return std::unexpected(Error{Errc::os, ERR_SITE(), e});
        }
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return uf;
}

constexpr std::size_t kUnzLocateNameMax = 256;

int zip_locate_long(unzFile uf, std::string_view member) {
    int rc = ::unzGoToFirstFile(uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        if (::unzGetCurrentFileInfo64(uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
            return UNZ_ERRNO;
        }
        if (static_cast<std::size_t>(fi.size_filename) == member.size() &&
            ieq(zip_current_name(uf, fi), member)) {
            return UNZ_OK;
        }
        rc = ::unzGoToNextFile(uf);
    }
    return (rc == UNZ_END_OF_LIST_OF_FILE) ? UNZ_END_OF_LIST_OF_FILE : rc;
}

int zip_locate(unzFile uf, const std::string& member) {
    if (member.size() < kUnzLocateNameMax) {
        return ::unzLocateFile(uf, member.c_str(), kZipCaseInsensitive);
    }
    return zip_locate_long(uf, member);
}

constexpr std::uint64_t kMaxDeflateRatio = 1032;
constexpr std::uint64_t kDeflateSlack = 1u << 16;

bool zip_size_is_possible(const unz_file_info64& fi) noexcept {
    const auto comp = static_cast<std::uint64_t>(fi.compressed_size);
    const auto unc = static_cast<std::uint64_t>(fi.uncompressed_size);
    switch (fi.compression_method) {
        case 0:
            return unc <= comp;
        case 8:
            break;
        default:
            return true;
    }
    if (comp > (UINT64_MAX - kDeflateSlack) / kMaxDeflateRatio) return true;
    return unc <= comp * kMaxDeflateRatio + kDeflateSlack;
}

struct ZipListing {
    std::vector<std::string> names;
    std::vector<std::uint64_t> sizes;
    std::vector<char> dirs;
};

Ex<ZipListing> zip_list(const std::string& archive) {
    auto uf = zip_open_archive(archive);
    if (!uf) return std::unexpected(uf.error());

    ZipListing out;
    unz_global_info64 gi{};
    if (::unzGetGlobalInfo64(*uf, &gi) == UNZ_OK && gi.number_entry <= kZipReserveCap) {
        const auto n = static_cast<std::size_t>(gi.number_entry);
        out.names.reserve(n);
        out.sizes.reserve(n);
        out.dirs.reserve(n);
    }

    int rc = ::unzGoToFirstFile(*uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        if (::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }

        if (!zip_size_is_possible(fi)) {
            rc = ::unzGoToNextFile(*uf);
            continue;
        }
        std::string nm = zip_current_name(*uf, fi);
        out.sizes.push_back(static_cast<std::uint64_t>(fi.uncompressed_size));
        out.dirs.push_back(zip_name_is_dir(nm) ? char{1} : char{0});
        out.names.push_back(std::move(nm));
        rc = ::unzGoToNextFile(*uf);
    }
    ::unzClose(*uf);

    if (rc != UNZ_END_OF_LIST_OF_FILE) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(rc)});
    }
    return out;
}

class ZipFile final : public IFile {
public:
    ZipFile(unzFile uf, std::string member, std::uint64_t size, std::string archive,
            std::uint64_t packed_off, std::uint64_t packed_len,
            std::optional<unz64_file_pos> at = std::nullopt)
        : uf_(uf), member_(std::move(member)), size_(size), archive_(std::move(archive)),
          packed_off_(packed_off), packed_len_(packed_len), at_(at) {}

    ~ZipFile() override {
        if (stream_open_) ::unzCloseCurrentFile(uf_);
        ::unzClose(uf_);
    }

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;

    Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= size_) return std::size_t{0};
        if (off < pos_) {
            if (auto r = rewind(); !r) return std::unexpected(r.error());
        }
        if (off > pos_) {
            if (auto r = skip_to(off); !r) return std::unexpected(r.error());
            if (pos_ != off) {
                return std::unexpected(
                    Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(pos_)});
            }
        }
        std::size_t done = 0;
        while (done < dst.size()) {
            const std::size_t want = std::min(dst.size() - done, kZipReadChunk);
            const int n = ::unzReadCurrentFile(uf_, dst.data() + done, static_cast<unsigned>(want));
            if (n < 0) {
                return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
            }
            if (n == 0) break;
            done += static_cast<std::size_t>(n);
            pos_ += static_cast<std::uint64_t>(n);
        }
        return done;
    }

    Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    Ex<FileSize> size() const override { return FileSize{size_}; }
    Ex<void> flush() override { return {}; }
    FileKind kind() const noexcept override { return FileKind::Zip; }

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        if (a != Access::Sequential || packed_len_ == 0) return {};
        UniqueFd fd{::open(archive_.c_str(), O_RDONLY | O_CLOEXEC)};
        if (fd.get() < 0)
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        const int rc = ::posix_fadvise(fd.get(), static_cast<::off_t>(packed_off_),
                                       static_cast<::off_t>(packed_len_), POSIX_FADV_WILLNEED);
        if (rc != 0)
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(rc)});
        return {};
    }

private:
    Ex<void> rewind() {
        if (stream_open_) {
            ::unzCloseCurrentFile(uf_);
            stream_open_ = false;
        }

        const int rc = at_ ? ::unzGoToFilePos64(uf_, &*at_) : zip_locate(uf_, member_);
        if (rc != UNZ_OK) {
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        }
        if (::unzOpenCurrentFile(uf_) != UNZ_OK) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        stream_open_ = true;
        pos_ = 0;
        return {};
    }

    Ex<void> skip_to(std::uint64_t target) {
        std::byte scratch[4096];
        while (pos_ < target) {
            const std::uint64_t want = std::min<std::uint64_t>(target - pos_, sizeof scratch);
            const int n = ::unzReadCurrentFile(uf_, scratch, static_cast<unsigned>(want));
            if (n < 0) {
                return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
            }
            if (n == 0) break;
            pos_ += static_cast<std::uint64_t>(n);
        }
        return {};
    }

    unzFile uf_;
    std::string member_;
    std::uint64_t size_ = 0;
    std::string archive_;
    std::uint64_t packed_off_ = 0;
    std::uint64_t packed_len_ = 0;
    std::uint64_t pos_ = 0;
    std::optional<unz64_file_pos> at_;
    bool stream_open_ = true;
};

Ex<std::unique_ptr<IFile>> zip_wrap_current(unzFile uf, const unz_file_info64& fi,
                                            const std::string& fallback_name,
                                            const std::string& archive,
                                            std::optional<unz64_file_pos> at = std::nullopt) {
    if (!zip_size_is_possible(fi)) {
        ::unzClose(uf);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    std::string stored = zip_current_name(uf, fi);

    if (zip_name_is_dir(stored)) {
        ::unzClose(uf);
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EISDIR)});
    }
    if (::unzOpenCurrentFile(uf) != UNZ_OK) {
        ::unzClose(uf);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (stored.empty()) stored = fallback_name;
    const auto packed_off = static_cast<std::uint64_t>(::unzGetCurrentFileZStreamPos64(uf));
    return std::unique_ptr<IFile>(
        new ZipFile(uf, std::move(stored), static_cast<std::uint64_t>(fi.uncompressed_size),
                    archive, packed_off, static_cast<std::uint64_t>(fi.compressed_size), at));
}

Ex<std::unique_ptr<IFile>> zip_open_member(const std::string& archive, const std::string& member) {
    auto uf = zip_open_archive(archive);
    if (!uf) return std::unexpected(uf.error());

    if (zip_locate(*uf, member) != UNZ_OK) {
        ::unzClose(*uf);
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    unz_file_info64 fi{};
    if (::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
        ::unzClose(*uf);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return zip_wrap_current(*uf, fi, member, archive);
}

Ex<std::unique_ptr<IFile>> zip_open_member_by_crc(const std::string& archive, std::uint32_t crc) {
    auto uf = zip_open_archive(archive);
    if (!uf) return std::unexpected(uf.error());
    int rc = ::unzGoToFirstFile(*uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        if (::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        if (static_cast<std::uint32_t>(fi.crc) == crc)
            return zip_wrap_current(*uf, fi, {}, archive);
        rc = ::unzGoToNextFile(*uf);
    }
    ::unzClose(*uf);
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

struct ZipEntries {
    std::vector<ArchiveEntry> entries;
    std::vector<unz64_file_pos> at;
};

[[nodiscard]] Ex<ZipEntries> zip_entries(const std::string& archive, std::size_t max) {
    auto uf = zip_open_archive(archive);
    if (!uf) return std::unexpected(uf.error());
    unz_global_info64 gi{};
    if (::unzGetGlobalInfo64(*uf, &gi) != UNZ_OK) {
        ::unzClose(*uf);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (gi.number_entry > max) {
        ::unzClose(*uf);
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(E2BIG)});
    }
    ZipEntries out;
    const auto n =
        static_cast<std::size_t>(std::min<std::uint64_t>(gi.number_entry, kZipReserveCap));
    out.entries.reserve(n);
    out.at.reserve(n);
    int rc = ::unzGoToFirstFile(*uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        unz64_file_pos at{};
        if (out.entries.size() == max ||
            ::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
            ::unzGetFilePos64(*uf, &at) != UNZ_OK) {
            ::unzClose(*uf);
            const auto d = out.entries.size() == max ? static_cast<std::uint32_t>(E2BIG) : 0u;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
        }
        ArchiveEntry e{.name = zip_current_name(*uf, fi),
                       .size = static_cast<std::uint64_t>(fi.uncompressed_size),
                       .packed = static_cast<std::uint64_t>(fi.compressed_size),
                       .crc = static_cast<std::uint32_t>(fi.crc),
                       .method = static_cast<std::uint16_t>(fi.compression_method),
                       .encrypted = (fi.flag & 1u) != 0u,
                       .plausible = zip_size_is_possible(fi)};
        if (e.name.size() != fi.size_filename) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        out.entries.push_back(std::move(e));
        out.at.push_back(at);
        rc = ::unzGoToNextFile(*uf);
    }
    ::unzClose(*uf);
    if (rc != UNZ_END_OF_LIST_OF_FILE) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(rc)});
    }
    return out;
}

class ZipArchive final : public IArchive {
public:
    ZipArchive(std::string archive, ZipEntries z)
        : archive_(std::move(archive)), z_(std::move(z)) {}

    std::span<const ArchiveEntry> entries() const noexcept override { return z_.entries; }

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open(std::size_t index) const override {
        if (index >= z_.entries.size())
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        const ArchiveEntry& e = z_.entries[index];
        if (e.encrypted || (e.method != 0 && e.method != 8)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), e.method});
        }
        auto uf = zip_open_archive(archive_);
        if (!uf) return std::unexpected(uf.error());
        unz_file_info64 fi{};
        if (::unzGoToFilePos64(*uf, &z_.at[index]) != UNZ_OK ||
            ::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }

        if (static_cast<std::uint64_t>(fi.uncompressed_size) != e.size ||
            zip_current_name(*uf, fi) != e.name) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }
        return zip_wrap_current(*uf, fi, e.name, archive_, z_.at[index]);
    }

private:
    std::string archive_;
    ZipEntries z_;
};

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
    if (f) (void)(*f)->advise(IFile::Access::Sequential, 0, 0);
    return f;
}

bool mode_creates(OpenMode m) {
    return m == OpenMode::Create || m == OpenMode::Truncate || m == OpenMode::Sync;
}

constexpr ::mode_t kCreateMode = S_IRWXU | S_IRWXG | S_IRWXO;

}  // namespace

namespace detail {

Admit admit_entry(std::string_view name, bool is_dir, bool at_root, const ScanFilter& f) {
    if (f.romset_browse) {

        if (!is_dir && iends_with(name, ".zip")) is_dir = true;
        if (!iends_with(name, ".neo") && !is_dir) return Admit::Reject;
        if (name == "..") {
            if (at_root) return Admit::Reject;
        } else if (istarts_with(name, ".")) {
            return Admit::Reject;
        }
        return is_dir ? Admit::KeepDir : Admit::KeepFile;
    }

    if (is_dir) {

        if (name == "System Volume Information") return Admit::Reject;
        if (name == "..") {
            if (at_root) return Admit::Reject;
        } else if (istarts_with(name, ".")) {
            return Admit::Reject;
        }

        return f.directories ? Admit::KeepDir : Admit::Reject;
    }

    if (istarts_with(name, ".")) return Admit::Reject;
    if (ieq(name, "menu.rbf")) return Admit::Reject;
    if (istarts_with(name, "menu_20")) return Admit::Reject;
    if (istarts_with(name, "boot") && iends_with(name, ".rom")) {
        constexpr std::size_t kBootRomLen = 8;
        constexpr std::size_t kBootRomDigitLen = 9;
        constexpr std::size_t kBootRomDigitPos = 4;
        const bool digit = name.size() == kBootRomDigitLen && name[kBootRomDigitPos] >= '0' &&
                           name[kBootRomDigitPos] <= '9';
        if (name.size() == kBootRomLen || digit) return Admit::Reject;
    }

    if (!f.extensions.empty()) {

        if (f.zip_as_directory && f.directories && iends_with(name, ".zip")) {
            return Admit::KeepDir;
        }
        if (!Vfs::extension_matches(name, f.extensions)) return Admit::Reject;
    }
    return f.files ? Admit::KeepFile : Admit::Reject;
}

bool is_dot_component(std::string_view n) noexcept { return n == "." || n == ".."; }

std::vector<DirEntry> zip_folder_entries(std::string_view folder,
                                         std::span<const ZipMember> members, const ScanFilter& f) {
    std::vector<DirEntry> out;

    std::set<std::string, std::less<>> seen_dirs;
    const auto already_seen = [&seen_dirs](std::string_view n) {
        return seen_dirs.find(n) != seen_dirs.end();
    };

    for (const ZipMember& m : members) {

        if (const auto rel = relative_member(folder, m.name)) {
            const std::size_t slash = rel->find('/');
            if (slash != std::string_view::npos) {
                const std::string_view dirname = rel->substr(0, slash);
                if (!rel->empty() && rel->front() != '/' && !is_dot_component(dirname) &&
                    !already_seen(dirname)) {
                    seen_dirs.emplace(dirname);
                    if (f.directories) {
                        DirEntry e;
                        e.name = std::string(dirname);
                        e.is_dir = true;
                        e.is_zip_member = true;
                        out.push_back(std::move(e));
                    }
                }
            }
        }

        if (!in_same_folder(folder, m.name)) continue;

        std::string_view sub = m.name.substr(folder.size());
        if (!sub.empty() && sub.front() == '/') sub.remove_prefix(1);
        if (sub.empty()) continue;

        if (m.is_dir) continue;

        const detail::Admit verdict = admit_entry(sub, false, folder.empty(), f);
        if (verdict == Admit::Reject) continue;

        DirEntry e;
        e.name = std::string(sub);
        e.is_dir = (verdict == Admit::KeepDir);
        e.is_zip_member = true;
        e.size = FileSize{m.size};
        out.push_back(std::move(e));
    }

    DirEntry up;
    up.name = "..";
    up.is_dir = true;
    up.is_zip_member = true;
    out.push_back(std::move(up));

    std::stable_sort(out.begin(), out.end(), dirent_less);
    return out;
}

bool dirent_less(const DirEntry& a, const DirEntry& b) noexcept {

    const bool a_up = a.is_dir && a.name == "..";
    const bool b_up = b.is_dir && b.name == "..";
    if (a_up || b_up) {
        if (a_up && b_up) return false;
        return a_up;
    }
    if (a.is_dir != b.is_dir) return a.is_dir;

    std::size_t l1 = a.name.size();
    std::size_t l2 = b.name.size();
    if (l1 > 4 && a.name[l1 - 4] == '.') l1 -= 4;
    if (l2 > 4 && b.name[l2 - 4] == '.') l2 -= 4;

    const int r = icompare_n(a.name, b.name, (l1 < l2) ? l1 : l2);
    if (r != 0) return r < 0;
    if (l1 != l2) return l1 < l2;

    return false;
}

}  // namespace detail

Ex<Vfs> Vfs::create(StorageRoot root) {
    Vfs v;
    if (!root.usb) {
        v.root_ = StorageRoot{false, 0};
        v.root_path_ = std::string(kSdRoot);
        return v;
    }

    for (std::uint8_t i = 0; i < kUsbRootSlots; ++i) {
        if (usb_mounted(i)) {
            v.root_ = StorageRoot{true, i};
            v.root_path_ = usb_path(i);
            return v;
        }
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<Vfs> Vfs::create_at(std::string_view root_path) {
    if (root_path.empty() || root_path.front() != '/') {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EINVAL)});
    }
    Vfs v;
    v.root_ = StorageRoot{false, 0};
    v.root_path_ = std::string(root_path);
    while (v.root_path_.size() > 1 && v.root_path_.back() == '/')
        v.root_path_.pop_back();
    if (!path_is_dir(v.root_path_)) {
        return std::unexpected(
            Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(ENOENT)});
    }
    return v;
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
        if (path_exists(cand)) return cand;
        if (!policy.case_insensitive) return std::nullopt;
        if (auto folded = fold_case_lookup(base, rel)) return folded;
        return std::nullopt;
    };

    for (const SearchDir d : policy.order) {
        switch (d) {
            case SearchDir::CurrentRoot:
                if (auto p = probe(root_path_)) return *p;
                break;
            case SearchDir::SdRoot:

                if (auto p = probe(kSdRoot)) return *p;
                break;
            case SearchDir::UsbPrefixed:

                for (std::uint8_t i = 0; i < kUsbSearchSlots; ++i) {
                    if (auto p = probe(usb_path(i))) return *p;
                }
                break;
            case SearchDir::Network:
                if (auto p = probe(kNetworkDir)) return *p;
                break;
            case SearchDir::Cifs:
                if (auto p = probe(compose(root_path_, kCifsDir))) return *p;
                break;
            case SearchDir::CoreDir:

                if (auto p = probe(compose(root_path_, kGamesDir))) return *p;
                break;
            case SearchDir::ConfigDir:
                if (auto p = probe(compose(root_path_, kConfigDir))) return *p;
                break;
        }
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<std::unique_ptr<IFile>> Vfs::open(std::string_view path, OpenMode mode) const {
    auto f = open_backend_(path, mode);
    if (mode == OpenMode::ReadWhole) return hinted_whole(std::move(f));
    return f;
}

Ex<std::unique_ptr<IFile>> Vfs::open_backend_(std::string_view path, OpenMode mode) const {
    const std::string full = compose(root_path_, path);

    for (const MemoryBlob& m : memory_) {
        if (m.path == full) {
            if (!opens_read_only(mode)) {
                return std::unexpected(
                    Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
            }
            return std::unique_ptr<IFile>(new MemoryFile(m.blob, m.hints));
        }
    }

    const ZipSplit zs = detail::zip_split(full);
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

    struct ::stat st {};
    if (::fstat(fd.get(), &st) != 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }

    FileKind kind = FileKind::Real;
    std::optional<std::uint64_t> fixed;

    if (istarts_with(full, "/dev/shm/")) {

        kind = FileKind::Shm;
    } else if (S_ISBLK(st.st_mode) || (st.st_rdev != 0 && st.st_size == 0)) {

        kind = FileKind::BlockDev;
        unsigned long long blksize = 0;
        if (::ioctl(fd.get(), BLKGETSIZE64, &blksize) < 0) {
            const auto e = static_cast<std::uint32_t>(errno);
            return std::unexpected(Error{Errc::os, ERR_SITE(), e});
        }
        fixed = static_cast<std::uint64_t>(blksize);
    }

    const bool unlink_at_close = (kind == FileKind::Shm) && mode_creates(mode);
    return std::unique_ptr<IFile>(
        new PosixFile(static_cast<UniqueFd&&>(fd), kind, fixed, full, unlink_at_close));
}

Ex<void> Vfs::mount_memory(std::string_view path, std::span<const std::byte> blob, Crc32 crc) {
    if (path.empty()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EINVAL)});
    }
    std::string key = compose(root_path_, path);
    for (MemoryBlob& m : memory_) {
        if (m.path == key) {
            m.blob = blob;
            m.crc = crc;
            return {};
        }
    }
    memory_.push_back(
        MemoryBlob{std::move(key), blob, crc, std::make_shared<std::atomic<std::uint32_t>>(0u)});
    return {};
}

std::uint32_t Vfs::memory_sequential_hints(std::string_view path) const noexcept {
    const std::string key = compose(root_path_, path);
    for (const MemoryBlob& m : memory_) {
        if (m.path == key) return m.hints->load(std::memory_order_relaxed);
    }
    return 0;
}

Ex<std::unique_ptr<IFile>> Vfs::open_zip_by_crc(std::string_view path, Crc32 crc) const {
    if (crc.v != 0) {
        const std::string full = compose(root_path_, path);
        const ZipSplit zs = detail::zip_split(full);
        if (zs.zipped && !zs.member.empty() && !detail::has_nested_zip(full)) {

            const std::string prefix = std::string(zs.archive) + "/";
            for (const MemoryBlob& m : memory_) {
                if (m.crc == crc && m.path.size() > prefix.size() &&
                    std::string_view(m.path).substr(0, prefix.size()) == prefix) {
                    return std::unique_ptr<IFile>(new MemoryFile(m.blob, m.hints));
                }
            }
            if (auto hit = zip_open_member_by_crc(std::string(zs.archive), crc.v)) {
                return hit;
            }
        }
    }
    return open(path, OpenMode::Read);
}

Ex<std::unique_ptr<IArchive>> Vfs::open_archive(std::string_view path,
                                                std::size_t max_entries) const {
    const std::string full = compose(root_path_, path);
    auto entries = zip_entries(full, max_entries);
    if (!entries) return std::unexpected(entries.error());
    return std::unique_ptr<IArchive>(new ZipArchive(full, std::move(*entries)));
}

bool Vfs::dir_exists(std::string_view path) const noexcept {
    return path_is_dir(compose(root_path_, path));
}

bool Vfs::file_exists(std::string_view path) const noexcept {
    return path_is_regular(compose(root_path_, path));
}

Ex<std::vector<DirEntry>> Vfs::scan(std::string_view dir, const ScanFilter& f) const {
    const std::string full = compose(root_path_, dir);

    if (detail::has_nested_zip(full)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const ZipSplit zs = detail::zip_split(full);
    if (zs.zipped) {
        auto listing = zip_list(std::string(zs.archive));
        if (!listing) return std::unexpected(listing.error());

        std::vector<detail::ZipMember> members;
        members.reserve(listing->names.size());
        for (std::size_t i = 0; i < listing->names.size(); ++i) {
            members.push_back(
                detail::ZipMember{listing->names[i], listing->sizes[i], listing->dirs[i] != 0});
        }
        return detail::zip_folder_entries(zs.member, members, f);
    }

    ::DIR* d = ::opendir(full.c_str());
    if (d == nullptr) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }

    std::vector<DirEntry> out;
    const bool at_root = dir.empty();

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
        if (name == ".") continue;

        bool is_dir = false;
        bool known = false;
        bool have_st = false;
        struct ::stat st {};
        if (de->d_type == DT_DIR) {
            is_dir = true;
            known = true;
        } else if (de->d_type == DT_REG) {
            known = true;
        }
        if (de->d_type == DT_LNK || de->d_type == DT_REG || de->d_type == DT_UNKNOWN) {
            std::string child = full;
            child.push_back('/');
            child.append(name);
            if (::stat(child.c_str(), &st) == 0) {
                have_st = true;
                if (S_ISDIR(st.st_mode)) {
                    is_dir = true;
                    known = true;
                } else if (S_ISREG(st.st_mode)) {
                    is_dir = false;
                    known = true;
                }
            }
        }
        if (!known) continue;

        const detail::Admit verdict = detail::admit_entry(name, is_dir, at_root, f);
        if (verdict == detail::Admit::Reject) continue;

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
    if (!full.empty() && full.front() == '/') cur.push_back('/');

    std::size_t i = cur.empty() ? 0 : 1;
    while (i < full.size()) {
        std::size_t j = full.find('/', i);
        if (j == std::string::npos) j = full.size();
        const std::string_view comp(full.data() + i, j - i);
        i = j + 1;
        if (comp.empty()) continue;

        if (!cur.empty() && cur.back() != '/') cur.push_back('/');
        cur.append(comp);

        if (::mkdir(cur.c_str(), kCreateMode) == 0) {
            sync_dir_of_(cur);
        } else if (errno != EEXIST) {
            const auto e = static_cast<std::uint32_t>(errno);
            return std::unexpected(Error{Errc::os, ERR_SITE(), e});
        }
    }
    return {};
}

Ex<void> Vfs::replace(std::string_view tmp_rel, std::string_view final_rel) const {
    const std::string from = compose(root_path_, tmp_rel);
    const std::string to = compose(root_path_, final_rel);
    if (::rename(from.c_str(), to.c_str()) != 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    g_dura_last_rename.store(dura_ticket(), std::memory_order_relaxed);
    g_dura_renames.fetch_add(1, std::memory_order_relaxed);

    sync_dir_of_(to);
    return {};
}

void Vfs::sync_parent_dir(std::string_view rel) const noexcept {
    sync_dir_of_(compose(root_path_, rel));
}

void Vfs::sync_dir_of_(const std::string& to) noexcept {
    std::string dir = to;
    const std::size_t slash = dir.rfind('/');
    dir = (slash == std::string::npos) ? std::string(".")
          : (slash == 0)               ? std::string("/")
                                       : dir.substr(0, slash);
    if (const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dfd >= 0) {
        const bool ok = ::fsync(dfd) == 0 || errno == EINVAL || errno == EROFS;
        (void)::close(dfd);
        if (ok) g_dura_dir_syncs.fetch_add(1, std::memory_order_relaxed);
    }
}

Vfs::DurabilityLog Vfs::durability_log() noexcept {
    DurabilityLog out;
    out.file_syncs = g_dura_file_syncs.load(std::memory_order_relaxed);
    out.dir_syncs = g_dura_dir_syncs.load(std::memory_order_relaxed);
    out.renames = g_dura_renames.load(std::memory_order_relaxed);
    out.last_file_sync = g_dura_last_file_sync.load(std::memory_order_relaxed);
    out.last_rename = g_dura_last_rename.load(std::memory_order_relaxed);
    return out;
}

bool Vfs::extension_matches(std::string_view filename, std::string_view packed_extensions) {

    const std::size_t dot = filename.rfind('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view fext = filename.substr(dot + 1);

    std::size_t pos = 0;
    while (pos < packed_extensions.size()) {

        char e[4] = {'\0', '\0', '\0', '\0'};
        const std::size_t avail = packed_extensions.size() - pos;
        const std::size_t n = (avail < 3) ? avail : 3;
        for (std::size_t k = 0; k < n; ++k)
            e[k] = packed_extensions[pos + k];
        if (e[2] == ' ') {
            e[2] = '\0';
            if (e[1] == ' ') e[1] = '\0';
        }

        bool found = true;
        for (std::size_t i = 0; i < 4; ++i) {
            if (e[i] == '*') break;
            const char c = (i < fext.size()) ? fext[i] : '\0';
            if (e[i] == '?' && c != '\0') continue;
            if (to_lower(e[i]) != to_lower(c)) found = false;

            if (e[i] == '\0' || !found) break;
        }
        if (found) return true;

        if (avail < 3) break;
        pos += 3;
    }
    return false;
}

std::optional<Crc32> Vfs::parse_crc_name(std::string_view filename, std::string_view ext) {

    const std::size_t len = filename.size();
    const std::size_t extlen = ext.size();
    if (len < 10 + extlen) return std::nullopt;
    if (!iends_with(filename, ext)) return std::nullopt;

    const std::size_t bracket = len - 10 - extlen;
    if (filename[bracket] != '[') return std::nullopt;
    if (filename[len - extlen - 1] != ']') return std::nullopt;

    const auto v = detail::scan_hex8(filename.substr(bracket + 1, 8));
    if (!v) return std::nullopt;
    return Crc32{*v};
}

namespace {

bool valid_asset(const std::string& p, bool require_zip = false) {
    if (!path_is_regular(p)) return false;
    if (!require_zip) return true;
    auto uf = zip_open_archive(p);
    if (!uf) return false;
    ::unzClose(*uf);
    return true;
}

std::optional<std::string> asset_in_same_dir(const std::string& rom_full, std::string_view ext) {
    const std::size_t slash = rom_full.rfind('/');
    if (slash == std::string::npos) return std::nullopt;
    const std::string dir = rom_full.substr(0, slash);

    ::DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return std::nullopt;
    std::string best;
    while (const ::dirent* de = ::readdir(d)) {
        const std::string_view name(de->d_name);
        if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN) continue;
        if (!iends_with(name, ext)) continue;
        if (!best.empty() && !(name < std::string_view(best))) continue;

        if (de->d_type == DT_UNKNOWN && !path_is_regular(dir + "/" + std::string(name))) {
            continue;
        }
        best = name;
    }
    ::closedir(d);
    if (best.empty()) return std::nullopt;
    return dir + "/" + best;
}

std::optional<std::string> asset_by_crc(Crc32 crc, std::string_view ext, const std::string& dir) {
    if (crc.v == 0) return std::nullopt;
    ::DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return std::nullopt;
    std::string best;
    while (const ::dirent* de = ::readdir(d)) {
        const std::string_view name(de->d_name);
        if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN) continue;
        const auto parsed = Vfs::parse_crc_name(name, ext);
        if (!parsed || !(*parsed == crc)) continue;
        if (!best.empty() && !(name < std::string_view(best))) continue;
        if (de->d_type == DT_UNKNOWN && !path_is_regular(dir + "/" + std::string(name))) {
            continue;
        }
        best = name;
    }
    ::closedir(d);
    if (best.empty()) return std::nullopt;
    return dir + "/" + best;
}

std::string retarget_extension(std::string p, std::string_view infix, std::string_view ext) {
    const std::size_t dot = p.rfind('.');
    if (dot != std::string::npos) p.resize(dot);
    p.append(infix);
    p.append(ext);
    return p;
}

}  // namespace

Ex<std::string> Vfs::find_game_asset(const AssetQuery& q) const {

    const bool cd_layout = !q.cd_asset_dir.empty();

    const std::string core_dir = compose(root_path_, q.core_dir);
    const std::string cd_dir = cd_layout ? compose(root_path_, q.cd_asset_dir) : core_dir;

    std::string path;
    if (ifind(q.rom_path, q.ext) == std::string_view::npos) {
        path = retarget_extension(compose(root_path_, q.rom_path), "", q.ext);
    }
    if (!path.empty() && valid_asset(path, q.require_zip)) return path;

    if (cd_layout) {
        const std::string rom_full = compose(root_path_, q.rom_path);
        if (auto p = asset_in_same_dir(rom_full, q.ext)) {
            if (valid_asset(*p, q.require_zip)) return *p;
        }
    }

    const std::size_t slash = q.rom_path.rfind('/');
    if (slash != std::string_view::npos) {
        std::string cand(cd_dir);
        cand.append(q.rom_path.substr(slash));
        cand = retarget_extension(std::move(cand), cd_layout ? " []" : "", q.ext);
        if (valid_asset(cand, q.require_zip)) return cand;
    }

    if (auto p = asset_by_crc(q.crc, q.ext, core_dir)) {
        if (valid_asset(*p, q.require_zip)) return *p;
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

}  // namespace mister::svc
