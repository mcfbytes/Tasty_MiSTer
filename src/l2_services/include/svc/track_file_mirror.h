// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/mirror_source.h"

namespace mister::svc {

inline constexpr proto::FileSize kTrackFilePieceBytes{64 * 1024};

struct TrackFileMirrorExtent {
    proto::FileSize size;
    proto::FileSize room;
};

class TrackFileMirror final : public IMirrorSource {
    TASTY_SEAT_RESIDENT(Main);

public:
    TrackFileMirror(std::unique_ptr<IFile> file, TrackFileMirrorExtent extent) noexcept;

    [[nodiscard]] Ex<std::optional<MirrorPiece>> next() override;

private:
    std::unique_ptr<IFile> file_;
    proto::FileSize size_{};
    proto::FileSize room_{};
    proto::FileOffset at_{};
    std::array<std::byte, kTrackFilePieceBytes.file_word<std::size_t>()> buf_{};
};

}
