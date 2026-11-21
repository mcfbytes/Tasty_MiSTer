// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx/psx_sector_program.h"

#include "cores/psx/psx_sector_map.h"
#include "cores/psx/psx_staging.h"

namespace mister::cores::psx {

proto::SlotIndex PsxSectorProgram::slot() const noexcept {
    return psx::kPsxStaging.disc_slot.block_slot();
}

proto::BlockSize PsxSectorProgram::frame_bytes() const noexcept {
    return proto::BlockSize{static_cast<std::uint32_t>(svc::disc::kRawFrameBytes)};
}

svc::disc::DiscForm PsxSectorProgram::form() const noexcept {
    return svc::disc::DiscForm::RawFrame;
}

PsxSectorProgram::Source PsxSectorProgram::answer(
    const svc::disc::DiscGeometry& disc,
    proto::Lba lba
) const noexcept {
    const PsxSectorMap::Answer a = PsxSectorMap{disc}.classify(lba);
    if (a == PsxSectorMap::Answer::Engine)
        return Source{.engine = true, .fill = 0xAA};
    return Source{.engine = false, .fill = PsxSectorMap::fill_byte(a)};
}

}
