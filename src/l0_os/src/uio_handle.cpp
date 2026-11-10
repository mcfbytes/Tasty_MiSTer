// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/uio_handle.h"

#include "os/path_buf.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace mister::os {
namespace {

constexpr unsigned kMaxUioNodes = 64;

[[nodiscard]] Ex<void> read_uio_name(const char* root, unsigned idx, std::span<char> out) {
    if (out.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    char path[kUioSysfsPathBuf];
    if (!format_path(std::span<char>(path), "%s/uio%u/name", root, idx)) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(sizeof path)}
        );
    }

    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    const ssize_t got = ::read(fd, out.data(), out.size() - 1);
    ::close(fd);
    if (got <= 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(got)});
    }

    std::size_t len = static_cast<std::size_t>(got);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        --len;
    out[len] = '\0';
    return {};
}

}

std::optional<UioMinor> find_uio_node_under(
    const char* sysfs_root,
    std::string_view name
) noexcept {
    char want[kUioNodeNameBuf];
    if (name.empty() || name.size() >= sizeof want)
        return std::nullopt;
    std::memcpy(want, name.data(), name.size());
    want[name.size()] = '\0';

    for (unsigned i = 0; i < kMaxUioNodes; ++i) {
        char have[kUioNodeNameBuf];
        if (!read_uio_name(sysfs_root, i, std::span<char>(have)))
            continue;
        if (std::string_view{have} == want)
            return UioMinor{i};
    }
    return std::nullopt;
}

std::optional<UioMinor> find_uio_node(std::string_view name) noexcept {
    return find_uio_node_under(kUioClassRoot, name);
}

namespace {

std::optional<UioMinor> find_doorbell_uio_node(const UioLineSpace& space, UioLine line) {
    char want[kUioNodeNameBuf];
    if (!format_path(
            std::span<char>(want),
            "%.*s%u",
            static_cast<int>(space.node_prefix.size()),
            space.node_prefix.data(),
            to_ordinal(line) + 1
        )) {
        return std::nullopt;
    }
    char root[kUioSysfsPathBuf];
    if (!format_path(
            std::span<char>(root),
            "%.*s",
            static_cast<int>(space.sysfs_root.size()),
            space.sysfs_root.data()
        )) {
        return std::nullopt;
    }
    return find_uio_node_under(root, want);
}

}

Ex<UioHandle> UioHandle::open(UioLine line, const UioLineSpace& space) {
    const std::uint32_t n = to_ordinal(line);
    if (!space.contains(line) || space.node_prefix.empty()) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    const auto idx = find_doorbell_uio_node(space, line);
    if (!idx) {

        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    char path[kUioDevPathBuf];
    if (!format_path(
            std::span<char>(path),
            "%.*s/uio%s",
            static_cast<int>(space.dev_dir.size()),
            space.dev_dir.data(),
            idx->text().c_str()
        )) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), n});
    }

    const int fd = ::open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return std::unexpected(infra::errno_error(Errc::uio_open, ERR_SITE(), errno));
    }
    return adopt(line, UniqueFd{fd});
}

Ex<UioHandle> UioHandle::adopt(UioLine line, UniqueFd fd) {
    if (!fd.valid()) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), to_ordinal(line)});
    }

    const int flags = ::fcntl(fd.get(), F_GETFL);
    if (flags < 0 || (flags & O_NONBLOCK) == 0) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), to_ordinal(line)});
    }
    UioHandle h;
    h.fd_ = std::move(fd);
    h.line_ = line;
    return h;
}

}
