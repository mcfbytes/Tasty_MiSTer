// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_sector_program.h"

#include "cores/manifests/psx.h"
#include "cores/psx_sector_map.h"

namespace mister::cores {

proto::SlotIndex PsxSectorProgram::slot() const noexcept {
    return manifests::kPsx.staging.disc_slot.block_slot();
}

std::uint32_t PsxSectorProgram::frame_bytes() const noexcept {
    return static_cast<std::uint32_t>(svc::kCdDataSize);
}

svc::DiscForm PsxSectorProgram::form() const noexcept {
    return svc::DiscForm::RawFrame;
}

PsxSectorProgram::Source PsxSectorProgram::answer(
    const svc::DiscGeometry& disc,
    std::int64_t lba
) const noexcept {
    const PsxSectorMap::Answer a = PsxSectorMap{disc}.classify(lba);
    if (a == PsxSectorMap::Answer::Engine)
        return Source{.engine = true, .fill = 0xAA};
    return Source{.engine = false, .fill = PsxSectorMap::fill_byte(a)};
}

}
