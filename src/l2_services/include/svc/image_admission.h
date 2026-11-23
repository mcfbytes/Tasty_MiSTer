// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/file.h"
#include "svc/image_facts.h"
#include "svc/mount_refusal.h"

namespace mister::svc {

enum class FluxSide : std::uint8_t { Inch525, Inch35 };

enum class FluxAdmission : std::uint8_t { Passthrough, CopyWhole, Convert };

enum class BlockAdmission : std::uint8_t { Passthrough, TwoMgPayload, Dc42Payload };

[[nodiscard]] std::expected<FluxAdmission, MountRefusal> admit_flux_image(
    FluxSide drive,
    const ImageFacts& facts
) noexcept;
[[nodiscard]] std::expected<BlockAdmission, MountRefusal> admit_block_image(
    const ImageFacts& facts
) noexcept;

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> open_flux_image(
    FluxSide drive,
    std::unique_ptr<IFile> file,
    std::string_view path,
    proto::FileSize size,
    bool writable
);
[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> open_block_image(
    std::unique_ptr<IFile> file,
    std::string_view path,
    proto::FileSize size,
    bool writable
);

}
