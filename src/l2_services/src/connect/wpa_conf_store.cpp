// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/connect/wpa_conf_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "infra/errno.h"
#include "infra/file_mode.h"
#include "infra/prelude.h"
#include "infra/rename_files.h"
#include "infra/unique_fd.h"
#include "os/replace_file.h"

namespace mister::svc::connect {
namespace {

constexpr std::string_view kTmpSuffix = ".tasty-tmp";

inline constexpr std::size_t kReadChunkBytes = 4096;

[[nodiscard]] std::unexpected<Error> os_error(infra::Errno e) noexcept {
    return std::unexpected(e.refusal(Errc::os, ERR_SITE()));
}

}

Ex<WpaConfStoreLoaded> WpaConfStore::load() const {
    UniqueFd fd{::open(path_.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) {
        const int e = errno;
        if (e == ENOENT)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        return os_error(infra::Errno{e});
    }

    std::string text;
    std::array<char, kReadChunkBytes> buf{};
    while (true) {
        const ::ssize_t n = ::read(fd.get(), buf.data(), buf.size());
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return os_error(infra::Errno{errno});
        }
        if (n == 0)
            break;
        text.append(buf.data(), buf.data() + n);
    }

    struct stat st{};
    if (::fstat(fd.get(), &st) != 0)
        return os_error(infra::Errno{errno});

    auto file = WpaConfFile::parse(text);
    if (!file)
        return std::unexpected(file.error());
    return WpaConfStoreLoaded{std::move(*file), ConfStamp::from_stat(st)};
}

Ex<WpaConfStoreSaveResult> WpaConfStore::save(const WpaConfFile& file, ConfStamp stamp) const {
    struct stat st{};
    ConfStamp current;
    if (::stat(path_.c_str(), &st) == 0) {
        current = ConfStamp::from_stat(st);
    } else if (errno == ENOENT) {
        current = ConfStamp::absent();
    } else {
        return os_error(infra::Errno{errno});
    }
    if (!(current == stamp))
        return WpaConfStoreSaveResult::Stale;

    const std::string tmp = path_ + std::string{kTmpSuffix};
    const std::string text = file.text();

    if (const auto replaced = os::replace_file(
            infra::RenameFiles{tmp, path_},
            text,
            infra::kPrivateFileMode,
            os::ReplaceSync::File
        );
        !replaced) {
        return std::unexpected(replaced.error());
    }
    return WpaConfStoreSaveResult::Saved;
}

}
