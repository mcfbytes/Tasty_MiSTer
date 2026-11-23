// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/mac.h"

#include "cores/mac/blue_scsi_changer.h"
#include "cores/mac/blue_scsi_toolbox.h"
#include "cores/mac/mac_cd_image.h"
#include "cores/mac/mac_core.h"
#include "cores/mac/toolbox_slot.h"
#include "proto/types.h"

namespace mister::cores::manifests {

static_assert(mac::kToolboxSlot == proto::SlotIndex{3});
static_assert(mac::kCdSlot == proto::SlotIndex{4});
static_assert(mac::kChangerSlot == proto::SlotIndex{5});

static_assert(mac::kCdAudioLba == proto::Lba{0x40000000u});
static_assert(mac::kCdTocLba == proto::Lba{0x7FFF0000u});
static_assert(mac::kCdToc.blocks == proto::BlockCount{2});
static_assert(mac::kCdAudio.end() == mac::kCdTocLba, "the audio window runs up to the TOC block");

static_assert(mac::kCdAudio.block_size == proto::BlockSize{2352});
static_assert(kMacLc.block_sector_floor.is_sector(2352));

static_assert(mac::kBlueScsiTailBlocks == 128);
static_assert(mac::kToolboxRpc.end() == proto::Lba{129});

static_assert(mac::kCdData.end_byte() <= mac::kCdToc.first_byte());
static_assert(mac::kCdToc.end_byte() <= mac::kCdAudio.first_byte());
static_assert(lba_windows_joined(kMacLc.lba_windows));

static_assert(kMacLc.repulse.after == std::chrono::milliseconds{60000});
static_assert(kMacLc.repulse.slot == mac::kCdSlot);

static_assert(kMacLcControlSlots[0].slot != kMacLcControlSlots[1].slot);
static_assert(kMacLc.undeclared_role == SlotRole::Save);

std::unique_ptr<Core> make_maclc(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<mac::MacCore>(p, g);
}

std::unique_ptr<svc::IStorageBackend> make_maclc_toolbox(const SlotServantTerms& t) {
    return std::make_unique<mac::ToolboxSlot<mac::BlueScsiToolbox>>(t.vfs, mac::BlueScsiToolbox{});
}

std::unique_ptr<svc::IStorageBackend> make_maclc_cd(const SlotServantTerms& t) {
    return std::make_unique<mac::MacCdImage>(t.vfs, t.mount_reads);
}

std::unique_ptr<svc::IStorageBackend> make_maclc_changer(const SlotServantTerms& t) {
    return std::make_unique<mac::ToolboxSlot<mac::BlueScsiChanger>>(
        t.vfs,
        mac::BlueScsiChanger{mac::kCdSlot, t.remount}
    );
}

}
