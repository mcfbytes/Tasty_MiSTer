// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_ladder.h"

#include <algorithm>
#include <ctime>
#include <variant>

#include "cores/config_codec.h"
#include "cores/host_bit_row.h"
#include "cores/x86/x86_bus_poll.h"
#include "cores/x86/x86_sys_config.h"
#include "infra/prelude.h"
#include "os/local_time.h"
#include "proto/link_op.h"
#include "proto/uio_opcodes.h"
#include "svc/card_dirs.h"
#include "svc/file.h"
#include "svc/read_bounded.h"
#include "svc/vfs.h"

namespace mister::cores::x86 {

namespace {

static_assert(X86Slot::kCount <= kLadderCarrySlots, "every media slot's path rides the carry");
constexpr proto::BlockSize kFloppySector{512};
constexpr proto::DiskUnit kDriveB = *kFloppyB.floppy();

}

X86Ladder::X86Ladder(const CoreProfile& profile, const LadderContext& ctx, const X86Family& family)
    : BootLadder(profile, ctx),
      family_(family) {
    const std::size_t n = std::min(ctx.slot_paths.size(), paths_.size());
    std::copy_n(ctx.slot_paths.begin(), n, paths_.begin());
    ide_left_ = kX86Slots;
    switch (ctx.kind()) {
        case LadderContext::ArmKind::CoreStart:

            reads_config_ = true;
            whole_init_ = true;
            rung_ = Rung::Paths;
            break;
        case LadderContext::ArmKind::Reset:
        case LadderContext::ArmKind::Restore:

            whole_init_ = true;
            rung_ = Rung::Hold;
            break;
        case LadderContext::ArmKind::Mount: {
            const auto& mount = std::get<LadderContext::MountArm>(ctx.arm);
            const auto slot = X86Slot::of(mount.slot);
            if (!slot)
                break;
            path_(*slot).assign(mount.image);
            pick_ = slot;
            const auto drive = slot->floppy();
            if (!drive) {
                rung_ = Rung::BindPick;
            } else if (family_.drives.contains(*drive)) {
                drive_ = *drive;
                rung_ = Rung::Mount;
            }
            break;
        }
        case LadderContext::ArmKind::kCount:
            break;
    }
}

Ex<bool> X86Ladder::on_step() {
    TASTY_SEAT_BODY(X86Ladder);
    switch (rung_) {
        case Rung::Paths:
            read_paths_();
            rung_ = Rung::Hold;
            return false;
        case Rung::Hold:
            if (!hold_reset_())
                return false;
            rung_ = family_.roms == X86BootRoms::DdrShadow ? Rung::Shadow : Rung::Mount;
            if (rung_ == Rung::Mount) {
                if (const auto first = family_.drives.lowest()) {
                    drive_ = *first;
                } else {
                    rung_ = Rung::IdeBinds;
                }
            }
            return false;
        case Rung::Shadow:
            return shadow_rung_();
        case Rung::ShadowWait:
            return shadow_wait_();
        case Rung::Mount:
            return mount_rung_();
        case Rung::Eject:
            if (!write_(FloppyMedia::eject(kFdcDriveBase[drive_])))
                return false;
            gap_until_ = now() + kEjectGap;
            rung_ = Rung::Gap;
            return false;
        case Rung::Gap:
            return gap_rung_();
        case Rung::Insert:
            return insert_rung_();
        case Rung::Cmos:
            return cmos_rung_();
        case Rung::IdeBinds:
            return ide_binds_rung_();
        case Rung::Release:

            if (!share_asked_) {
                host_.reset_share();
                share_asked_ = true;
            }
            if (!release_reset_())
                return false;
            rung_ = Rung::Done;
            return true;
        case Rung::BindPick:
            return bind_pick_();
        case Rung::Done:
            return true;
    }
    return true;
}

void X86Ladder::read_paths_() {
    if (!reads_config_ || !profile_.config_codec)
        return;
    const IConfigCodec& codec = *profile_.config_codec;
    const auto name = codec.file_name(kStartupSlot);
    if (name.view().empty())
        return;
    const std::string rel = svc::card_rel({svc::kConfigDir, name.view()});
    std::array<std::byte, X86SysConfig::kBytes> img{};
    if (auto f = vfs_.open(rel, svc::OpenMode::ReadWhole)) {
        const auto read = svc::read_bounded(**f, img.size());
        if (read)
            std::copy_n(read->begin(), std::min(read->size(), img.size()), img.begin());
    }
    const X86SysConfig cfg = X86SysConfig::decode(img);
    for (const X86Slot slot : kX86Slots)
        path_(slot).assign(cfg.path(slot));
}

Ex<bool> X86Ladder::shadow_rung_() {
    if (!walk_gen_)
        walk_gen_ = next_gen();
    if (!host_.order_walk(kShadowWalk, profile_.name, walk_gen_))
        return false;
    rung_ = Rung::ShadowWait;
    return false;
}

Ex<bool> X86Ladder::shadow_wait_() {
    const MountStatus s = host_.walk_status();
    if (s.act != walk_gen_ || s.state == MountState::Pending)
        return false;
    walk_gen_ = {};
    rung_ = Rung::Mount;
    if (const auto first = family_.drives.lowest()) {
        drive_ = *first;
    } else {
        rung_ = Rung::IdeBinds;
    }
    return false;
}

Ex<bool> X86Ladder::mount_rung_() {
    const auto slot = X86Slot::of(drive_);
    if (!slot)
        return std::unexpected(drive_.refusal(Errc::slot_range, ERR_SITE()));
    const std::string& path = path_(*slot);
    const MountState answer = mount_slot_bounded(slot->block_slot(), path);
    if (answer == MountState::Pending)
        return false;
    media_[drive_] = probe_(drive_, answer == MountState::Done && !path.empty());
    rung_ = Rung::Eject;
    return false;
}

FloppyMedia X86Ladder::probe_(proto::DiskUnit drive, bool opened) {
    if (!opened)
        return FloppyMedia::of(false, proto::BlockCount{});
    const auto slot = X86Slot::of(drive);
    if (!slot)
        return FloppyMedia::of(false, proto::BlockCount{});
    const std::string& path = path_(*slot);
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    const proto::BlockCount blocks = size ? kFloppySector.blocks_in(*size) : proto::BlockCount{};
    FloppyMedia m = FloppyMedia::of(true, blocks);

    m.writable = m.present && card_.open(path, svc::OpenMode::ReadWrite).has_value();
    return m;
}

Ex<bool> X86Ladder::gap_rung_() {
    if (now() < gap_until_)
        return false;
    insert_ = media_[drive_].insert(kFdcDriveBase[drive_]);
    insert_left_ = insert_;
    rung_ = Rung::Insert;
    return false;
}

Ex<bool> X86Ladder::insert_rung_() {
    while (!insert_left_.empty()) {
        if (!write_(insert_left_.front()))
            return false;
        insert_left_ = insert_left_.subspan(1);
    }
    next_drive_();
    return rung_ == Rung::Done;
}

void X86Ladder::next_drive_() noexcept {
    if (!whole_init_) {
        rung_ = Rung::Done;
        return;
    }
    if (drive_ != kDriveB && family_.drives.contains(kDriveB)) {
        drive_ = kDriveB;
        rung_ = Rung::Mount;
        return;
    }
    rung_ = Rung::IdeBinds;
}

X86CmosInputs X86Ladder::cmos_inputs_() const {
    X86CmosInputs in{};

    in.calendar = proto::RtcCalendar::of(os::local_now().value_or(std::tm{}));
    in.fdd0 = media_[proto::DiskUnit{0}];
    in.fdd1 = media_[proto::DiskUnit{1}];
    in.hdd0_named = !infra::at_index(paths_, kIde00).empty();
    in.hdd1_named = !infra::at_index(paths_, kIde01).empty();
    const bool small = family_.memory == X86Memory::Fixed16Mb
        || host_value(host_.status_word(), profile_.host_bits, HostFact::RamSize)
            != proto::FieldValue{};
    in.ram = small ? X86RamSize::Mb16 : X86RamSize::Mb256;
    return in;
}

Ex<bool> X86Ladder::cmos_rung_() {
    if (!cmos_) {
        cmos_ = compose_x86_cmos(cmos_inputs_());
        cmos_at_ = {};
    }
    while (infra::affine::in_bounds(kCmosRegisters, cmos_at_)) {
        const BusRegisterWrite w{
            kRtcBase + cmos_at_.offset(),
            hal::SpiWord::of_byte((*cmos_)[cmos_at_])
        };
        if (!write_(w))
            return false;
        cmos_at_ += CmosRegisterCount{1};
    }
    rung_ = Rung::Release;
    return false;
}

Ex<bool> X86Ladder::bind_pick_() {
    const auto ordered = order_bind_mount(pick_->io_index(), path_(*pick_));
    if (!ordered)
        return std::unexpected(ordered.error());
    if (!*ordered)
        return false;
    rung_ = Rung::Done;
    return true;
}

Ex<bool> X86Ladder::ide_binds_rung_() {
    TASTY_SEAT_BODY(X86Ladder);
    while (!ide_left_.empty()) {
        const X86Slot slot = ide_left_.front();
        if (family_.declares(slot)) {
            const auto ordered = order_bind_mount(slot.io_index(), path_(slot));
            if (!ordered)
                return std::unexpected(ordered.error());
            if (!*ordered)
                return false;
        }
        ide_left_ = ide_left_.subspan(1);
    }
    rung_ = Rung::Cmos;
    return false;
}

bool X86Ladder::write_(const BusRegisterWrite& w) {
    return order_register_write(proto::UioOp::DmaWrite, w.mailbox_words());
}

}
