// SPDX-License-Identifier: GPL-3.0-or-later
#include "vfs_zip.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>

#include <minizip/unzip.h>

#include "infra/ascii.h"
#include "infra/unique_fd.h"

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

class ZipHandle {
public:
    [[nodiscard]] static Ex<ZipHandle> open(const std::string& archive) {
        auto uf = zip_open_archive(archive);
        if (!uf)
            return std::unexpected(uf.error());
        return ZipHandle{*uf};
    }

    ZipHandle(ZipHandle&& other) noexcept : uf_(std::exchange(other.uf_, nullptr)) {}

    ZipHandle(const ZipHandle&) = delete;
    ZipHandle& operator=(const ZipHandle&) = delete;
    ZipHandle& operator=(ZipHandle&&) = delete;

    ~ZipHandle() {
        if (uf_ != nullptr)
            ::unzClose(uf_);
    }

    [[nodiscard]] unzFile get() const noexcept { return uf_; }

private:
    explicit ZipHandle(unzFile uf) noexcept : uf_(uf) {}

    unzFile uf_;
};

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

constexpr std::uint16_t kZipEncryptedFlag = 1;

constexpr std::uint64_t kMaxDeflateRatio = 1032;
constexpr std::uint64_t kDeflateSlack = 1u << 16;

bool zip_size_is_possible(const unz_file_info64& fi) noexcept {
    const auto comp = fi.compressed_size;
    const auto unc = fi.uncompressed_size;

    const std::optional<ZipMethod> method = method_of(
        static_cast<std::uint16_t>(fi.compression_method)
    );
    if (!method)
        return true;
    if (*method == ZipMethod::Stored)
        return unc <= comp;
    if (comp > (UINT64_MAX - kDeflateSlack) / kMaxDeflateRatio)
        return true;
    return unc <= comp * kMaxDeflateRatio + kDeflateSlack;
}

[[nodiscard]] ArchiveLocator zip_locator_of(const unz64_file_pos& pos) noexcept {
    return ArchiveLocator{
        .directory_offset = proto::FileSize{pos.pos_in_zip_directory},
        .ordinal = MemberOrdinal{pos.num_of_file}
    };
}

[[nodiscard]] unz64_file_pos zip_pos_of(ArchiveLocator at) noexcept {
    const std::array<std::uint64_t, 2> words = at.to_unz();
    return unz64_file_pos{.pos_in_zip_directory = words[0], .num_of_file = words[1]};
}

[[nodiscard]] std::optional<ArchiveEntry> zip_member_record(
    unzFile uf,
    const unz_file_info64& fi,
    std::optional<ArchiveLocator> at
) {
    ArchiveEntry e{
        .name = zip_current_name(uf, fi),
        .size = fi.uncompressed_size,
        .packed = fi.compressed_size,
        .crc = Crc32{static_cast<std::uint32_t>(fi.crc)},
        .method = static_cast<std::uint16_t>(fi.compression_method),
        .encrypted = (fi.flag & kZipEncryptedFlag) != 0u,
        .plausible = zip_size_is_possible(fi),
        .at = at.value_or(ArchiveLocator{})
    };

    if (e.name.size() != fi.size_filename)
        return std::nullopt;
    return e;
}

using CentralDirectory = std::vector<ArchiveEntry>;

[[nodiscard]] Ex<CentralDirectory> zip_central_directory(
    unzFile uf,
    std::optional<std::size_t> max
) {
    unz_global_info64 gi{};
    if (::unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (max && gi.number_entry > *max) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(E2BIG)}
        );
    }
    CentralDirectory out;
    out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(gi.number_entry, kZipReserveCap)));
    int rc = ::unzGoToFirstFile(uf);
    while (rc == UNZ_OK) {
        unz_file_info64 fi{};
        unz64_file_pos pos{};
        const bool over = max && out.size() == *max;
        if (over
            || ::unzGetCurrentFileInfo64(uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK
            || ::unzGetFilePos64(uf, &pos) != UNZ_OK) {
            const auto d = over ? static_cast<std::uint32_t>(E2BIG) : 0u;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
        }
        std::optional<ArchiveEntry> e = zip_member_record(uf, fi, zip_locator_of(pos));
        if (!e)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        out.push_back(std::move(*e));
        rc = ::unzGoToNextFile(uf);
    }

    if (rc != UNZ_END_OF_LIST_OF_FILE) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(rc)});
    }
    return out;
}

}

[[nodiscard]] Ex<ZipListing> zip_list(const std::string& archive) {
    auto h = ZipHandle::open(archive);
    if (!h)
        return std::unexpected(h.error());
    auto rows = zip_central_directory(h->get(), std::nullopt);
    if (!rows)
        return std::unexpected(rows.error());

    ZipListing out;
    out.members.reserve(rows->size());
    for (ArchiveEntry& e : *rows) {

        if (!e.plausible)
            continue;
        ZipMember member{};
        member.name = std::move(e.name);
        member.size = proto::FileSize{e.size};
        member.is_dir = zip_name_is_dir(member.name);
        out.members.push_back(std::move(member));
    }
    return out;
}

namespace {

class ZipFile final : public IFile {
public:
    ZipFile(
        ZipHandle handle,
        std::string member,
        std::uint64_t size,
        std::string archive,
        std::uint64_t packed_off,
        std::uint64_t packed_len,
        std::optional<ArchiveLocator> at = std::nullopt
    )
        : uf_(std::move(handle)),
          member_(std::move(member)),
          size_(size),
          archive_(std::move(archive)),
          packed_off_(packed_off),
          packed_len_(packed_len),
          at_(at) {}

    ~ZipFile() override {
        if (stream_open_)
            ::unzCloseCurrentFile(uf_.get());
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
            const int n = ::unzReadCurrentFile(
                uf_.get(),
                dst.data() + done,
                static_cast<unsigned>(want)
            );
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
            ::unzCloseCurrentFile(uf_.get());
            stream_open_ = false;
        }

        int rc = UNZ_OK;
        if (at_) {
            const unz64_file_pos pos = zip_pos_of(*at_);
            rc = ::unzGoToFilePos64(uf_.get(), &pos);
        } else {
            rc = zip_locate(uf_.get(), member_);
        }
        if (rc != UNZ_OK) {
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        }
        if (::unzOpenCurrentFile(uf_.get()) != UNZ_OK) {
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
            const int n = ::unzReadCurrentFile(uf_.get(), scratch, static_cast<unsigned>(want));
            if (n < 0) {
                return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
            }
            if (n == 0)
                break;
            pos_ += static_cast<std::uint64_t>(n);
        }
        return {};
    }

    ZipHandle uf_;
    std::string member_;
    std::uint64_t size_ = 0;
    std::string archive_;
    std::uint64_t packed_off_ = 0;
    std::uint64_t packed_len_ = 0;
    std::uint64_t pos_ = 0;
    std::optional<ArchiveLocator> at_;
    bool stream_open_ = true;
};

Ex<std::unique_ptr<IFile>> zip_wrap_current(
    ZipHandle handle,
    const ArchiveEntry& entry,
    std::string_view fallback_name,
    const std::string& archive,
    std::optional<ArchiveLocator> at = std::nullopt
) {
    if (!entry.plausible) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (zip_name_is_dir(entry.name)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EISDIR)});
    }
    if (::unzOpenCurrentFile(handle.get()) != UNZ_OK) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    std::string stored = entry.name.empty() ? std::string(fallback_name) : entry.name;
    const std::uint64_t packed_off = ::unzGetCurrentFileZStreamPos64(handle.get());
    return std::unique_ptr<IFile>(new ZipFile(
        std::move(handle),
        std::move(stored),
        entry.size,
        archive,
        packed_off,
        entry.packed,
        at
    ));
}

}

Ex<std::unique_ptr<IFile>> zip_open_member(const std::string& archive, const std::string& member) {
    auto h = ZipHandle::open(archive);
    if (!h)
        return std::unexpected(h.error());

    if (zip_locate(h->get(), member) != UNZ_OK) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    unz_file_info64 fi{};
    if (::unzGetCurrentFileInfo64(h->get(), &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    std::optional<ArchiveEntry> e = zip_member_record(h->get(), fi, std::nullopt);
    if (!e)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return zip_wrap_current(std::move(*h), *e, member, archive);
}

Ex<std::unique_ptr<IFile>> zip_open_member_by_crc(const std::string& archive, Crc32 crc) {
    auto h = ZipHandle::open(archive);
    if (!h)
        return std::unexpected(h.error());
    auto rows = zip_central_directory(h->get(), std::nullopt);
    if (!rows)
        return std::unexpected(rows.error());

    auto hit = rows->end();
    for (auto it = rows->begin(); it != rows->end(); ++it) {
        if (it->crc != crc)
            continue;
        hit = it;
        break;
    }
    if (hit == rows->end())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    const unz64_file_pos pos = zip_pos_of(hit->at);
    if (::unzGoToFilePos64(h->get(), &pos) != UNZ_OK) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return zip_wrap_current(std::move(*h), *hit, {}, archive);
}

namespace {

class ZipArchive final : public IArchive {
public:
    ZipArchive(std::string archive, CentralDirectory rows)
        : archive_(std::move(archive)),
          rows_(std::move(rows)) {}

    std::span<const ArchiveEntry> entries() const noexcept override { return rows_; }

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open(const ArchiveEntry& entry) const override {
        if (entry.encrypted || !method_of(entry.method)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), entry.method});
        }
        auto h = ZipHandle::open(archive_);
        if (!h)
            return std::unexpected(h.error());
        const unz64_file_pos pos = zip_pos_of(entry.at);
        unz_file_info64 fi{};
        if (::unzGoToFilePos64(h->get(), &pos) != UNZ_OK) {
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }
        if (::unzGetCurrentFileInfo64(h->get(), &fi, nullptr, 0, nullptr, 0, nullptr, 0)
            != UNZ_OK) {
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }

        if (fi.uncompressed_size != entry.size || zip_current_name(h->get(), fi) != entry.name) {
            return std::unexpected(Error{Errc::stale, ERR_SITE(), 0});
        }
        return zip_wrap_current(std::move(*h), entry, entry.name, archive_, entry.at);
    }

private:
    std::string archive_;
    CentralDirectory rows_;
};

}

bool zip_archive_opens(const std::string& archive) {
    return ZipHandle::open(archive).has_value();
}

Ex<std::unique_ptr<IArchive>> zip_make_archive(const std::string& archive, std::size_t max) {
    auto h = ZipHandle::open(archive);
    if (!h)
        return std::unexpected(h.error());
    auto rows = zip_central_directory(h->get(), max);
    if (!rows)
        return std::unexpected(rows.error());
    return std::unique_ptr<IArchive>(new ZipArchive(archive, std::move(*rows)));
}

}
