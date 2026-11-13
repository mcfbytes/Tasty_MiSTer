// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::svc {

using proto::FileSize;

enum class OpenMode : std::uint8_t { Read, ReadWhole, ReadWrite, Create, Truncate, Sync };

[[nodiscard]] constexpr bool opens_read_only(OpenMode m) noexcept {
    return m == OpenMode::Read || m == OpenMode::ReadWhole;
}

class IFile {
public:
    virtual ~IFile() = default;

    [[nodiscard]] virtual Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(
        std::uint64_t off,
        std::span<const std::byte> src
    ) = 0;

    template <class Off>
        requires requires(Off o) {
            { o.template file_word<std::uint64_t>() } -> std::same_as<std::uint64_t>;
        }
    [[nodiscard]] auto read_at(Off off, std::span<std::byte> dst) {
        return read_at(off.template file_word<std::uint64_t>(), dst);
    }

    [[nodiscard]] virtual Ex<FileSize> size() const = 0;
    [[nodiscard]] virtual Ex<void> flush() = 0;

    enum class Access : std::uint8_t { Sequential, WillNeed };

    [[nodiscard]] virtual Ex<void> advise(Access, std::uint64_t off, std::uint64_t len) {
        (void)off;
        (void)len;
        return {};
    }

    [[nodiscard]] Ex<void> advise(Access access, FileSize off, FileSize len) {
        return advise(access, off.file_word<std::uint64_t>(), len.file_word<std::uint64_t>());
    }

protected:
    IFile() = default;
    IFile(const IFile&) = default;
    IFile& operator=(const IFile&) = default;
};

[[nodiscard]] inline Ex<void> read_exact(IFile& f, proto::FileSize off, std::span<std::byte> dst) {
    std::size_t got = 0;
    while (got < dst.size()) {
        const auto n = f.read_at(proto::detail::positional_word(off), dst.subspan(got));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
        off += proto::FileSize{*n};
        got += *n;
    }
    return {};
}

[[nodiscard]] inline Ex<void> write_all(
    IFile& f,
    proto::FileSize off,
    std::span<const std::byte> src
) {
    std::size_t put = 0;
    while (put < src.size()) {
        const auto n = f.write_at(proto::detail::positional_word(off), src.subspan(put));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
        off += proto::FileSize{*n};
        put += *n;
    }
    return {};
}

}
