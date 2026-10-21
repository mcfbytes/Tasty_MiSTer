// SPDX-License-Identifier: GPL-3.0-or-later
#include "vfs_zip.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>

#include <minizip/unzip.h>

#include "infra/ascii.h"
#include "infra/unique_fd.h"
#include "vfs_detail.h"

namespace mister::svc::detail {

using infra::ieq;

namespace {

constexpr int kZipCaseInsensitive = 2;

constexpr std::size_t kZipReadChunk = 1u << 20;

constexpr std::uint64_t kZipReserveCap = 1u << 20;

bool zip_name_is_dir(std::string_view n) noexcept {
    return !n.empty() && (n.back() == '/' || n.back() == '\\');
}

std::string zip_current_name(unzFile uf, const unz_file_info64& fi) {
    std::string nm(static_cast<std::size_t>(fi.size_filename), '\0');
    if (fi.size_filename != 0
        && ::unzGetCurrentFileInfo64(
               uf,
               nullptr,
               nm.data(),
               fi.size_filename,
               nullptr,
               0,
               nullptr,
               0
           ) != UNZ_OK) {
        nm.clear();
    }
    return nm;
}

[[nodiscard]] Ex<unzFile> zip_open_archive(const std::string& archive) {
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
        if (static_cast<std::size_t>(fi.size_filename) == member.size()
            && ieq(zip_current_name(uf, fi), member)) {
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
    if (comp > (UINT64_MAX - kDeflateSlack) / kMaxDeflateRatio)
        return true;
    return unc <= comp * kMaxDeflateRatio + kDeflateSlack;
}

}

[[nodiscard]] Ex<ZipListing> zip_list(const std::string& archive) {
    auto uf = zip_open_archive(archive);
    if (!uf)
        return std::unexpected(uf.error());

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

namespace {

class ZipFile final : public IFile {
public:
    ZipFile(
        unzFile uf,
        std::string member,
        std::uint64_t size,
        std::string archive,
        std::uint64_t packed_off,
        std::uint64_t packed_len,
        std::optional<unz64_file_pos> at = std::nullopt
    )
        : uf_(uf),
          member_(std::move(member)),
          size_(size),
          archive_(std::move(archive)),
          packed_off_(packed_off),
          packed_len_(packed_len),
          at_(at) {}

    ~ZipFile() override {
        if (stream_open_)
            ::unzCloseCurrentFile(uf_);
        ::unzClose(uf_);
    }

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= size_)
            return std::size_t{0};
        if (off < pos_) {
            if (auto r = rewind(); !r)
                return std::unexpected(r.error());
        }
        if (off > pos_) {
            if (auto r = skip_to(off); !r)
                return std::unexpected(r.error());
            if (pos_ != off) {
                return std::unexpected(
                    Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(pos_)}
                );
            }
        }
        std::size_t done = 0;
        while (done < dst.size()) {
            const std::size_t want = std::min(dst.size() - done, kZipReadChunk);
            const int n = ::unzReadCurrentFile(uf_, dst.data() + done, static_cast<unsigned>(want));
            if (n < 0) {
                return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
            }
            if (n == 0)
                break;
            done += static_cast<std::size_t>(n);
            pos_ += static_cast<std::uint64_t>(n);
        }
        return done;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    [[nodiscard]] Ex<FileSize> size() const override { return FileSize{size_}; }

    [[nodiscard]] Ex<void> flush() override { return {}; }

    FileKind kind() const noexcept override { return FileKind::Zip; }

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        if (a != Access::Sequential || packed_len_ == 0)
            return {};
        UniqueFd fd{::open(archive_.c_str(), O_RDONLY | O_CLOEXEC)};
        if (fd.get() < 0)
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        const int rc = ::posix_fadvise(
            fd.get(),
            static_cast<::off_t>(packed_off_),
            static_cast<::off_t>(packed_len_),
            POSIX_FADV_WILLNEED
        );
        if (rc != 0)
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(rc)});
        return {};
    }

private:
    [[nodiscard]] Ex<void> rewind() {
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

    [[nodiscard]] Ex<void> skip_to(std::uint64_t target) {
        std::byte scratch[4096];
        while (pos_ < target) {
            const std::uint64_t want = std::min<std::uint64_t>(target - pos_, sizeof scratch);
            const int n = ::unzReadCurrentFile(uf_, scratch, static_cast<unsigned>(want));
            if (n < 0) {
                return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
            }
            if (n == 0)
                break;
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

Ex<std::unique_ptr<IFile>> zip_wrap_current(
    unzFile uf,
    const unz_file_info64& fi,
    const std::string& fallback_name,
    const std::string& archive,
    std::optional<unz64_file_pos> at = std::nullopt
) {
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
    if (stored.empty())
        stored = fallback_name;
    const auto packed_off = static_cast<std::uint64_t>(::unzGetCurrentFileZStreamPos64(uf));
    return std::unique_ptr<IFile>(new ZipFile(
        uf,
        std::move(stored),
        static_cast<std::uint64_t>(fi.uncompressed_size),
        archive,
        packed_off,
        static_cast<std::uint64_t>(fi.compressed_size),
        at
    ));
}

}

Ex<std::unique_ptr<IFile>> zip_open_member(const std::string& archive, const std::string& member) {
    auto uf = zip_open_archive(archive);
    if (!uf)
        return std::unexpected(uf.error());

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
    if (!uf)
        return std::unexpected(uf.error());
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

namespace {

struct ZipEntries {
    std::vector<ArchiveEntry> entries;
    std::vector<unz64_file_pos> at;
};

[[nodiscard]] Ex<ZipEntries> zip_entries(const std::string& archive, std::size_t max) {
    auto uf = zip_open_archive(archive);
    if (!uf)
        return std::unexpected(uf.error());
    unz_global_info64 gi{};
    if (::unzGetGlobalInfo64(*uf, &gi) != UNZ_OK) {
        ::unzClose(*uf);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (gi.number_entry > max) {
        ::unzClose(*uf);
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(E2BIG)}
        );
    }
    ZipEntries out;
    const auto n = static_cast<std::size_t>(
        std::min<std::uint64_t>(gi.number_entry, kZipReserveCap)
    );
    out.entries.reserve(n);
    out.at.reserve(n);
    int rc = ::unzGoToFirstFile(*uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        unz64_file_pos at{};
        if (out.entries.size() == max
            || ::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK
            || ::unzGetFilePos64(*uf, &at) != UNZ_OK) {
            ::unzClose(*uf);
            const auto d = out.entries.size() == max ? static_cast<std::uint32_t>(E2BIG) : 0u;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
        }
        ArchiveEntry e{
            .name = zip_current_name(*uf, fi),
            .size = static_cast<std::uint64_t>(fi.uncompressed_size),
            .packed = static_cast<std::uint64_t>(fi.compressed_size),
            .crc = static_cast<std::uint32_t>(fi.crc),
            .method = static_cast<std::uint16_t>(fi.compression_method),
            .encrypted = (fi.flag & 1u) != 0u,
            .plausible = zip_size_is_possible(fi)
        };
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
        : archive_(std::move(archive)),
          z_(std::move(z)) {}

    std::span<const ArchiveEntry> entries() const noexcept override { return z_.entries; }

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open(std::size_t index) const override {
        if (index >= z_.entries.size())
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        const ArchiveEntry& e = z_.entries[index];
        if (e.encrypted || (e.method != 0 && e.method != 8)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), e.method});
        }
        auto uf = zip_open_archive(archive_);
        if (!uf)
            return std::unexpected(uf.error());
        unz_file_info64 fi{};
        if (::unzGoToFilePos64(*uf, &z_.at[index]) != UNZ_OK
            || ::unzGetCurrentFileInfo64(*uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }

        if (static_cast<std::uint64_t>(fi.uncompressed_size) != e.size
            || zip_current_name(*uf, fi) != e.name) {
            ::unzClose(*uf);
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }
        return zip_wrap_current(*uf, fi, e.name, archive_, z_.at[index]);
    }

private:
    std::string archive_;
    ZipEntries z_;
};

}

bool zip_archive_opens(const std::string& archive) {
    auto uf = zip_open_archive(archive);
    if (!uf)
        return false;
    ::unzClose(*uf);
    return true;
}

Ex<std::unique_ptr<IArchive>> zip_make_archive(const std::string& archive, std::size_t max) {
    auto entries = zip_entries(archive, max);
    if (!entries)
        return std::unexpected(entries.error());
    return std::unique_ptr<IArchive>(new ZipArchive(archive, std::move(*entries)));
}

}
