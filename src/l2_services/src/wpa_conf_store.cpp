// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/wpa_conf_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "infra/errno.h"
#include "infra/unique_fd.h"

namespace mister::svc {
namespace {

constexpr std::string_view kTmpSuffix = ".tasty-tmp";

struct ReadChunk {
    std::size_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t bytes = v;
        return bytes;
    }

    friend constexpr bool operator==(ReadChunk, ReadChunk) = default;
};

constexpr ReadChunk kReadChunk{4096};

struct FileMode {
    ::mode_t v = 0;

    [[nodiscard]] constexpr ::mode_t create_mode() const noexcept {
        const ::mode_t bits = v;
        return bits;
    }

    friend constexpr bool operator==(FileMode, FileMode) = default;
};

constexpr FileMode kFileMode{0600};

[[nodiscard]] std::unexpected<Error> os_error(infra::Errno e) noexcept {
    return std::unexpected(e.refused(Errc::os, ERR_SITE()));
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
    std::array<char, kReadChunk.size()> buf{};
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
    UniqueFd fd{
        ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, kFileMode.create_mode())
    };
    if (!fd.valid())
        return os_error(infra::Errno{errno});

    const std::string text = file.text();
    const char* at = text.data();
    const char* const end = at + text.size();
    while (at < end) {
        const ::ssize_t n = ::write(fd.get(), at, static_cast<std::size_t>(end - at));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            const int e = errno;
            (void)::unlink(tmp.c_str());
            return os_error(infra::Errno{e});
        }
        if (n == 0) {
            (void)::unlink(tmp.c_str());
            return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
        }
        at += n;
    }

    if (::fsync(fd.get()) != 0) {
        const int e = errno;
        (void)::unlink(tmp.c_str());
        return os_error(infra::Errno{e});
    }
    fd.reset();

    if (::rename(tmp.c_str(), path_.c_str()) != 0) {
        const int e = errno;
        (void)::unlink(tmp.c_str());
        return os_error(infra::Errno{e});
    }
    return WpaConfStoreSaveResult::Saved;
}

}
