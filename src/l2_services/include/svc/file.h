// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "os/positioned_io.h"
#include "proto/types.h"

namespace mister::svc {

using proto::FileSize;

enum class OpenMode : std::uint8_t { Read, ReadWhole, ReadWrite, Create, Truncate, Sync };

[[nodiscard]] constexpr bool opens_read_only(OpenMode m) noexcept {
    return m == OpenMode::Read || m == OpenMode::ReadWhole;
}

[[nodiscard]] constexpr bool mode_creates(OpenMode m) noexcept {
    return m == OpenMode::Create || m == OpenMode::Truncate || m == OpenMode::Sync;
}

[[nodiscard]] constexpr Error read_only_refusal(OpenMode m, infra::ErrorSite at) noexcept {
    return Error{Errc::contract, at, static_cast<std::uint32_t>(m)};
}

class IFile {
public:
    virtual ~IFile() = default;

    [[nodiscard]] virtual Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) = 0;
    [[nodiscard]] virtual Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) = 0;

    [[nodiscard]] Ex<void> read_exact_at(proto::FileOffset at, std::span<std::byte> dst);
    [[nodiscard]] Ex<void> write_all_at(proto::FileOffset at, std::span<const std::byte> src);

    [[nodiscard]] Ex<infra::ByteCount> read_prefix_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    );

    [[nodiscard]] virtual Ex<FileSize> size() const = 0;
    [[nodiscard]] virtual Ex<void> flush() = 0;

    using Access = os::Advice;

    [[nodiscard]] virtual Ex<void> advise(Access, proto::FileOffset, FileSize) { return {}; }

protected:
    IFile() = default;
    IFile(const IFile&) = default;
    IFile& operator=(const IFile&) = default;
};

[[nodiscard]] inline Ex<void> IFile::read_exact_at(proto::FileOffset at, std::span<std::byte> dst) {
    while (!dst.empty()) {
        const auto n = read_at(at, dst);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
        at += proto::FileSize::of(*n);
        dst = n->past_in(dst);
    }
    return {};
}

[[nodiscard]] inline Ex<void> IFile::write_all_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    while (!src.empty()) {
        const auto n = write_at(at, src);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
        at += proto::FileSize::of(*n);
        src = n->past_in(src);
    }
    return {};
}

[[nodiscard]] inline Ex<infra::ByteCount> IFile::read_prefix_at(
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    std::span<std::byte> left = dst;
    while (!left.empty()) {
        const auto n = read_at(at, left);
        if (!n || n->empty())
            break;
        at += proto::FileSize::of(*n);
        left = n->past_in(left);
    }
    return infra::ByteCount{dst.size() - left.size()};
}

}
