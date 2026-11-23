// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/file.h"
#include "svc/nib_track.h"

namespace mister::svc {

class NibCodec final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IFile> file,
        bool writable
    );

    [[nodiscard]] FileSize size() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;

private:
    struct Pending {
        std::optional<proto::FileOffset> track_start{};
        proto::FileSize filled{};
        std::array<std::byte, kNibTrackBytes.file_word<std::size_t>()> bytes{};
    };

    NibCodec(std::unique_ptr<IFile> file, bool writable) noexcept;
    [[nodiscard]] Ex<void> write_act_(proto::FileOffset at, std::span<const std::byte> act);

    std::unique_ptr<IFile> file_;
    bool writable_;
    std::array<std::byte, kNibImageBytes.file_word<std::size_t>()> image_{};
    Pending pending_{};
};

}
