// SPDX-License-Identifier: GPL-3.0-or-later
#include "posix_file.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "infra/prelude.h"
#include "os/positioned_io.h"
#include "os/sync.h"
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

Ex<infra::ByteCount> PosixFile::read_at(proto::FileOffset at, std::span<std::byte> dst) {

    return os::pread_at(fd_, dst, at.kernel_offset());
}

Ex<infra::ByteCount> PosixFile::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    return os::pwrite_at(fd_, src, at.kernel_offset());
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
    return os::sync(fd_, os::SyncKind::All);
}

Ex<void> PosixFile::advise(Access a, proto::FileOffset at, FileSize len) {
    return os::advise(fd_, at.kernel_offset(), len.kernel_length(), a);
}

}
