// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/g64_synthesis.h"
#include "svc/mirror_source.h"

namespace mister::svc {

class SectorMirror final : public IMirrorSource {
    TASTY_SEAT_RESIDENT(Main);

public:
    SectorMirror(proto::FileSize size, std::vector<std::byte> sectors);

    [[nodiscard]] Ex<std::optional<MirrorPiece>> next() override;

private:
    [[nodiscard]] static gcr::DiskId disk_id(const std::vector<std::byte>& sectors) noexcept;

    std::vector<std::byte> sectors_;
    G64Synthesis synth_;
    bool header_out_ = false;
};

}
