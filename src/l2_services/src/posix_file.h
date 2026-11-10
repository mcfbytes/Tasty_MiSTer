// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::svc::detail {

enum class FileKind : std::uint8_t { Real, BlockDev, Shm };

class PosixFile final : public IFile {
public:
    PosixFile(
        UniqueFd fd,
        FileKind kind,
        std::optional<std::uint64_t> fixed_size,
        std::string path,
        bool unlink_at_close
    );

    ~PosixFile() override;

    PosixFile(const PosixFile&) = delete;
    PosixFile& operator=(const PosixFile&) = delete;

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override;

    [[nodiscard]] Ex<std::size_t> write_at(
        std::uint64_t off,
        std::span<const std::byte> src
    ) override;

    [[nodiscard]] Ex<FileSize> size() const override;

    [[nodiscard]] Ex<void> flush() override;

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t off, std::uint64_t len) override;

private:
    UniqueFd fd_;
    std::string path_;
    std::optional<std::uint64_t> fixed_size_{};
    FileKind kind_ = FileKind::Real;
    bool unlink_at_close_ = false;

    static_assert(std::is_trivially_copyable_v<decltype(fixed_size_)>);
};

}
