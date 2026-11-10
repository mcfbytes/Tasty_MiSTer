// SPDX-License-Identifier: GPL-3.0-or-later
#include "posix_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "svc/durable_write.h"

namespace mister::svc::detail {

PosixFile::PosixFile(
    UniqueFd fd,
    FileKind kind,
    std::optional<std::uint64_t> fixed_size,
    std::string path,
    bool unlink_at_close
)
    : fd_(static_cast<UniqueFd&&>(fd)),
      path_(std::move(path)),
      fixed_size_(fixed_size),
      kind_(kind),
      unlink_at_close_(unlink_at_close) {}

PosixFile::~PosixFile() {

    if (unlink_at_close_ && !path_.empty())
        ::unlink(path_.c_str());
}

Ex<std::size_t> PosixFile::read_at(std::uint64_t off, std::span<std::byte> dst) {
    std::size_t done = 0;
    while (done < dst.size()) {
        const ::ssize_t n = ::pread(
            fd_.get(),
            dst.data() + done,
            dst.size() - done,
            static_cast<::off_t>(off + done)
        );
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }

        if (n == 0)
            break;
        done += static_cast<std::size_t>(n);
    }
    return done;
}

Ex<std::size_t> PosixFile::write_at(std::uint64_t off, std::span<const std::byte> src) {
    std::size_t done = 0;
    while (done < src.size()) {
        const ::ssize_t n = ::pwrite(
            fd_.get(),
            src.data() + done,
            src.size() - done,
            static_cast<::off_t>(off + done)
        );
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (n == 0) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(done)}
            );
        }
        done += static_cast<std::size_t>(n);
    }
    return done;
}

Ex<FileSize> PosixFile::size() const {

    if (fixed_size_)
        return FileSize{*fixed_size_};
    struct ::stat st{};
    if (::fstat(fd_.get(), &st) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return FileSize{static_cast<std::uint64_t>(st.st_size)};
}

Ex<void> PosixFile::flush() {

    DurableWrite::note_file_sync();
    if (::fsync(fd_.get()) != 0) {
        const int e = errno;

        if (e != EINVAL && e != EROFS)
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), e));
    }
    return {};
}

Ex<void> PosixFile::advise(Access a, std::uint64_t off, std::uint64_t len) {
    const int advice = (a == Access::Sequential) ? POSIX_FADV_SEQUENTIAL : POSIX_FADV_WILLNEED;
    const int rc = ::posix_fadvise(
        fd_.get(),
        static_cast<::off_t>(off),
        static_cast<::off_t>(len),
        advice
    );
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc));
    return {};
}

}
