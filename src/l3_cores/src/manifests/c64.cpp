// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/c64.h"

#include <string_view>

#include "cores/c64/c64_core.h"
#include "cores/c64/c64_loader.h"
#include "cores/generic_profile.h"
#include "proto/conf_switches.h"
#include "proto/reset_terms.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_c64(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<c64::C64Core>(p, g.services);
}

std::unique_ptr<ILoader> make_c64_loader(const LoaderContext& ctx) {
    return std::make_unique<c64::C64Loader>(ctx);
}

static_assert(kC64.kind == CoreKind::C64);
static_assert(
    kC64.name == std::string_view{"C64"},
    "user_io.cpp is_c64(): strcasecmp(orig_name, \"C64\")"
);
static_assert(kC64.services.empty(), "the cartridge half declares no wire row");
static_assert(
    kC64.slots.size() == 2
        && kC64.slots[0].index == IoIndex{0}
        && kC64.slots[1].index == IoIndex{1},
    "c64.sv: H7S0,D64G64T64D81,Mount #8; H0S1,...,Mount #9"
);
static_assert(
    kC64.disk_formats.data() == svc::kGcrFormatRows.data()
        && kC64.disk_formats.size() == svc::kGcrFormatRows.size(),
    "user_io.cpp:2163-2166 c64_openGCR: D64, D71, G64, G71; .t64 via c64_openT64"
);
static_assert(
    kC64.windows.size() == 2
        && kC64.slots[0].mirror
        && kC64.slots[1].mirror
        && kC64.slots[0].mirror->fits(kC64.windows)
        && kC64.slots[1].mirror->fits(kC64.windows),
    "c64.cpp c64_openGCR: the drive reads 0x30000000 + (idx & 1) * 0x200000"
);
static_assert(
    !kC64.no_stream_role,
    "c64_open_file: every pick the re-frame leaves alone is user_io_file_tx"
);
static_assert(
    kC64.undeclared_role == kGenericProfile.undeclared_role
        && kC64.undeclared_start_mount == kGenericProfile.undeclared_start_mount,
    "an S row this profile does not declare keeps the rowless core's routes"
);

static_assert(
    kC64.host_bits.size() == 1
        && kC64.host_bits[0].fact == HostFact::SaveUploadTrigger
        && kC64.host_bits[0].bits == proto::BitField{proto::StatusBit{61}, 1}
        && kC64.host_bits[0].dir == HostBitRow::Dir::Pulse,
    "captured CONF_STR: hAdBR[61],Save cartridge, an R row the host pulses; "
    "c64.sv ezfl_save = status[61] | ..."
);
static_assert(
    host_pulses(kC64.host_bits, HostFact::SaveUploadTrigger, proto::StatusBit{61})
        && !host_pulses(kC64.host_bits, HostFact::SaveUploadTrigger, proto::StatusBit{62})
        && !host_pulses(kC64.host_bits, HostFact::SaveUploadTrigger, proto::StatusBit{0}),
    "only the Save cartridge row's pulse is the trigger: bit 62 is the Autosave "
    "option, which the core itself reads at the OSD edge; bit 0 is the reset"
);

static_assert(
    c64::kDetachResetBit == proto::StatusBit{17} && c64::kVideoStandardBit == proto::StatusBit{2},
    "captured CONF_STR: R[17],Reset & Detach Cartridge; P1O[2],Video Standard,PAL,NTSC"
);
static_assert(
    c64::kResetTerms.status_trips({}, proto::StatusWord{}.with(proto::kResetBit, true))
        && c64::kResetTerms.status_trips({}, proto::StatusWord{}.with(c64::kDetachResetBit, true))
        && c64::kResetTerms.status_trips(proto::StatusWord{}.with(c64::kDetachResetBit, true), {}),
    "c64.sv: the core is in reset while status[0] or status[17] is set"
);
static_assert(
    c64::kResetTerms.status_trips({}, proto::StatusWord{}.with(c64::kVideoStandardBit, true))
        && c64::kResetTerms.status_trips(proto::StatusWord{}.with(c64::kVideoStandardBit, true), {})
        && !c64::kResetTerms.status_trips(
            proto::StatusWord{}.with(c64::kVideoStandardBit, true),
            proto::StatusWord{}.with(c64::kVideoStandardBit, true)
        ),
    "c64.sv: ntsc_prev != ntsc resets on either change of status[2], not on its level"
);
static_assert(
    c64::kResetTerms
        .buttons_trip({}, proto::ConfSwitches{}.set(proto::ConfSwitches::Bit::UserButton)),
    "c64.sv: buttons[1]"
);
static_assert(
    !c64::kResetTerms.status_trips({}, proto::StatusWord{}.with(proto::StatusBit{61}, true))
        && !c64::kResetTerms.status_trips({}, proto::StatusWord{}.with(proto::StatusBit{62}, true)),
    "the save trigger's own pulse and the autosave option reset nothing"
);

static_assert(
    c64::kChipPacketBytes == proto::FileSize{8192 + 16},
    "c64.cpp: crt_bank_size = 8192 + 16"
);
static_assert(
    c64::kEasyFlashImageBytes == proto::FileSize{64 + 128 * (8192 + 16)}
        && c64::kEasyFlashImageBytes == proto::FileSize{1'050'688},
    "c64.cpp: ezfl_size = 64 + (128 * crt_bank_size)"
);
static_assert(
    kC64.largest_save == c64::kEasyFlashImageBytes,
    "the storage seat's image buffer holds the whole framed image"
);
static_assert(
    c64::kEasyFlashWireBytes == proto::FileSize{128 * 8192},
    "c64_save_cart: 128 reads of 8192 bytes"
);
static_assert(
    c64::kEasyFlashUploadIndex == proto::WideIoIndex{99},
    "c64_save_cart: user_io_set_index(99)"
);
static_assert(
    c64::kEasyFlashBanks.image_bytes(c64::kEasyFlashWireBytes) == c64::kEasyFlashImageBytes
        && c64::kEasyFlashBanks.place(proto::kFileStart)
            == proto::kFileStart + proto::FileSize{64 + 16}
        && c64::kEasyFlashBanks.place(proto::kFileStart + proto::FileSize{8192})
            == proto::kFileStart + proto::FileSize{64 + 16 + 8192 + 16},
    "c64_save_cart: off = 64 + 16, then off += 8192 + 16 per read"
);

static_assert(
    c64::kEasyFlashHeads[0] == 0x43
        && c64::kEasyFlashHeads[3] == 0x50
        && c64::kEasyFlashHeads[6] == 0x20
        && c64::kEasyFlashHeads[7] == 0x10
        && c64::kEasyFlashHeads[9] == 0x02
        && c64::kEasyFlashHeads[14] == 0x20,
    "c64.cpp: ezfl_shdr"
);
static_assert(
    c64::kEasyFlashHeads[11] == 0
        && c64::kEasyFlashHeads[12] == 0x80
        && c64::kEasyFlashHeads[16 + 11] == 0
        && c64::kEasyFlashHeads[16 + 12] == 0xA0
        && c64::kEasyFlashHeads[32 + 11] == 1
        && c64::kEasyFlashHeads[32 + 12] == 0x80
        && c64::kEasyFlashHeads[127 * 16 + 11] == 63
        && c64::kEasyFlashHeads[127 * 16 + 12] == 0xA0,
    "ezfl_format: addr starts 0x80, addr ^= 0x20 per packet, bank++ when it wraps"
);

}
