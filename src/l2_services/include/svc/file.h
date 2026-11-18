// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

[[nodiscard]] constexpr Error read_only_refusal(OpenMode m, infra::ErrorSite at) noexcept {
    return Error{Errc::contract, at, static_cast<std::uint32_t>(m)};
}

class IFile {
public:
    virtual ~IFile() = default;

    [[nodiscard]] virtual Ex<std::size_t> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) = 0;

    [[nodiscard]] virtual Ex<FileSize> size() const = 0;
    [[nodiscard]] virtual Ex<void> flush() = 0;

    enum class Access : std::uint8_t { Sequential, WillNeed };

    [[nodiscard]] virtual Ex<void> advise(Access, proto::FileOffset, FileSize) { return {}; }

protected:
    IFile() = default;
    IFile(const IFile&) = default;
    IFile& operator=(const IFile&) = default;
};

[[nodiscard]] inline Ex<void> read_exact(IFile& f, proto::FileOffset at, std::span<std::byte> dst) {
    std::size_t got = 0;
    while (got < dst.size()) {
        const auto n = f.read_at(at, dst.subspan(got));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
        at += proto::FileSize{*n};
        got += *n;
    }
    return {};
}

[[nodiscard]] inline Ex<void> write_all(
    IFile& f,
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    std::size_t put = 0;
    while (put < src.size()) {
        const auto n = f.write_at(at, src.subspan(put));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
        at += proto::FileSize{*n};
        put += *n;
    }
    return {};
}

}
