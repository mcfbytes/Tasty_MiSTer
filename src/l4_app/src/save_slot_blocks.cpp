// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_slot_blocks.h"

#include <cstring>

#include "infra/byte_view.h"
#include "infra/prelude.h"
#include "svc/blank_save.h"

namespace mister::app {

svc::ISaveFilesOnIo::Save SaveSlotBlocks::save_of_(proto::SlotIndex slot) noexcept {
    const svc::ISaveFilesOnIo::Save f = saves_->save_on_io(slot);
    SaveState& s = rules_[slot];
    if (s.gen != f.gen) {
        s.gen = f.gen;
        const bool exists = f.extent.has_value();
        s.rule = proto::SaveSlotRule::at_mount(
            exists,
            f.extent.value_or(proto::FileSize{}),
            f.mount
        );
    }
    return f;
}

SaveSlotBlocks::Read SaveSlotBlocks::read_block(
    proto::SlotIndex slot,
    proto::BlockSize bytes,
    proto::Lba lba,
    std::span<std::byte> out
) noexcept {
    const std::span<std::uint8_t> block = infra::byte_cast<std::uint8_t>(out);
    const svc::ISaveFilesOnIo::Save f = save_of_(slot);
    const proto::SaveSlotRule& rule = rules_[slot].rule;
    if (f.file && !rule.deferred_create) {
        const auto n = f.file->read_at(bytes.position_of(lba), out);
        if (n && !n->empty()) {
            n->zero_past_in(block);
            return Read::File;
        }
    }
    if (!rule.deferred_create) {
        std::memset(block.data(), 0, block.size());
    } else {
        svc::fill_absent(f.blank, lba, block);
    }
    return Read::Blank;
}

SaveSlotBlocks::Write SaveSlotBlocks::write_block(
    proto::SlotIndex slot,
    proto::BlockSize bytes,
    proto::Lba lba,
    std::span<const std::byte> block
) noexcept {
    const svc::ISaveFilesOnIo::Save f = save_of_(slot);
    proto::SaveSlotRule& rule = rules_[slot].rule;
    const std::span<const std::uint8_t> src = infra::byte_cast<const std::uint8_t>(block);
    const proto::SaveSlotRule::Plan plan = rule.plan_write(lba, bytes, proto::BlockCount{1});
    if (!f.file)
        return Write::NoFile;

    switch (plan.act) {
        case proto::SaveSlotRule::Act::Discard:
            return Write::Discarded;
        case proto::SaveSlotRule::Act::Create:
            if (auto e = f.file->create(infra::byte_cast<const std::byte>(src.first(plan.len)));
                e) {
                rule.created(*e);
                return Write::Created;
            }
            return Write::Failed;
        case proto::SaveSlotRule::Act::Write:
            if (auto n = f.file->write_at(
                    plan.offset,
                    infra::byte_cast<const std::byte>(src.first(plan.len))
                );
                n && *n == infra::ByteCount{plan.len}) {
                rule.wrote(plan.offset, plan.len);
                return Write::Wrote;
            }
            return Write::Failed;
    }
    return Write::Failed;
}

}
