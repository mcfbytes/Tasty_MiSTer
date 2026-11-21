// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/errno.h"
#include "infra/opt_ref.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/file.h"
#include "svc/io_meter.h"
#include "svc/types.h"
#include "infra/seat.h"
#include "infra/tally.h"

namespace mister::svc {

class MemoryMounts {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    void mount(std::string key, std::span<const std::byte> blob, Crc32 crc);

    [[nodiscard]] bool mounted(std::string_view key) const noexcept;

    [[nodiscard]] std::unique_ptr<IFile> open(std::string_view key) const;

    [[nodiscard]] std::unique_ptr<IFile> open_by_crc_under(
        std::string_view prefix,
        Crc32 crc
    ) const;

    [[nodiscard]] infra::Tally sequential_hints(std::string_view key) const noexcept;

    enum class IoVerb : std::uint8_t { Open, Read, Write };
    [[nodiscard]] std::optional<Error> refusal(std::string_view key, IoVerb verb) const noexcept;

    [[nodiscard]] std::unique_ptr<IFile> observed(
        std::string_view key,
        std::unique_ptr<IFile> file
    ) const;

private:
    friend struct VfsTestAccess;

    struct Refusal {
        std::string path;
        IoVerb verb = IoVerb::Open;
        infra::Errno err{};
        FileSize grace{};
    };

    [[nodiscard]] infra::OptRef<const Refusal> find_refusal_(
        std::string_view key,
        IoVerb verb
    ) const noexcept;

    struct Blob {
        std::string path;
        std::span<const std::byte> blob;
        Crc32 crc = Crc32::kUndeclared;
        std::shared_ptr<std::atomic<std::uint32_t>> hints;
    };

    [[nodiscard]] const Blob* find_(std::string_view key) const noexcept;

    std::vector<Blob> blobs_;
    std::vector<Refusal> refusals_;
    infra::OptRef<IIoMeter> meter_{};
};

}
