// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/x86.h"

#include <ctime>
#include <memory>

#include "cores/core_grant.h"
#include "cores/ladder_context.h"
#include "cores/loader_context.h"
#include "cores/servant_grant.h"
#include "cores/x86/bus_servant.h"
#include "cores/x86/x86_floppy_servant.h"
#include "cores/x86/x86_boot_loader.h"
#include "cores/x86/x86_cmos.h"
#include "cores/x86/x86_core.h"
#include "cores/x86/x86_ladder.h"
#include "cores/x86/x86_share_host.h"
#include "proto/rtc_calendar.h"

namespace mister::cores::manifests {

namespace {

static_assert(
    kAo486Family.poll.kept() == proto::ReplyCount{15},
    "x86.cpp:762-777: the request word, two six-word task files, the floppy pair"
);
static_assert(
    kPcxtFamily.poll.kept() == proto::ReplyCount{9},
    "hps_ext.v: the PCXT family ties mgmt_req[5:3] to zero, one IDE channel"
);
static_assert(
    kPcjrFamily.poll.kept() == proto::ReplyCount{3},
    "hps_ext.v: PCjr ties mgmt_req[5:0] to zero, no IDE channel"
);
static_assert(
    x86::fdc_params_at(kAo486Family.poll) == proto::ReplyIndex{13}
    && x86::fdc_params_at(kPcxtFamily.poll) == proto::ReplyIndex{7}
    && x86::fdc_params_at(kPcjrFamily.poll) == proto::ReplyIndex{1}
);

static_assert(
    x86::kIde0Channel.task_file_at == proto::ReplyIndex{1},
    "x86.cpp:762-777: the head keeps one reply, then IDE 0's six words"
);
static_assert(
    x86::kIde1Channel.task_file_at == proto::ReplyIndex{7},
    "x86.cpp:762-777: IDE 1's six words follow IDE 0's"
);
static_assert(
    kAo486Family.ide
        && kAo486Family.ide->channels.size() == 2
        && kPcxtFamily.ide
        && kPcxtFamily.ide->channels.size() == 1
        && !kPcjrFamily.ide,
    "ao486 decodes two IDE channels, the PCXT family one, PCjr none"
);
static_assert(
    kAo486Family.ide->channels.size() <= x86::BusServant::kChannels,
    "BusServant composes every channel any x86 RTL decodes"
);
static_assert(
    kAo486Family.ide->channels[0].units.at(ide::IdeDriveSelect::Master) == proto::DiskUnit{2}
        && kAo486Family.ide->channels[0].units.at(ide::IdeDriveSelect::Slave) == proto::DiskUnit{3}
        && kAo486Family.ide->channels[1].units.at(ide::IdeDriveSelect::Master) == proto::DiskUnit{4}
        && kAo486Family.ide->channels[1].units.at(ide::IdeDriveSelect::Slave) == proto::DiskUnit{5},
    "the IDE slots are the media indices 2..5, in channel and drive order"
);

consteval bool ide_families_run_a_rung_before_their_binds() {
    for (const x86::X86Family* family : {&kAo486Family, &kPcxtFamily, &kPcjrFamily})
        if (family->ide && family->roms != x86::X86BootRoms::DdrShadow && family->drives.empty())
            return false;
    return true;
}

static_assert(
    ide_families_run_a_rung_before_their_binds(),
    "an IDE family runs a rung between Hold and IdeBinds, so its binds follow the "
    "hold's first Reset presentation"
);

static_assert(x86::kRtcBase == proto::DiskAddress{0xF400}, "x86.cpp:45 RTC_BASE");
static_assert(x86::kFdcBase == proto::DiskAddress{0xF200}, "x86.cpp:43 FDD0_BASE");
static_assert(
    x86::kFdcDriveBase[proto::DiskUnit{1}] == proto::DiskAddress{0xF280},
    "x86.cpp:461: drive B's block is FDD0_BASE + (1 << 7), not FDD1_BASE"
);
static_assert(x86::X86SysConfig::kBytes == 4 + 6 * 1024, "x86.cpp:206-210 x86_config");
static_assert(x86::X86SysConfig::kVersion == 3, "x86.cpp:41 CFG_VER");
static_assert(
    x86::X86Ladder::kEjectGap == std::chrono::milliseconds{100},
    "x86.cpp:464 usleep(100000)"
);
static_assert(
    kAo486.reset.reinits && kPcjr.reset.reinits && !kPc110.reset.reinits,
    "menu.cpp:2574 and user_io.cpp:3075: the x86 family's reset IS x86_init"
);
static_assert(
    kPc110.host.forces_gui_f12
        && kZ486.host.forces_gui_f12
        && kZ386.host.forces_gui_f12
        && kAo486.host.forces_gui_f12,
    "user_io.cpp is_f12_mod_needed: true for the whole family"
);
static_assert(
    kPcxt.host.keys.gui_chords_own && !kAo486.host.keys.gui_chords_own,
    "user_io.cpp:3867: only the PCXT family swallows the Win key's chords"
);

constexpr x86::X86CmosInputs kEmptyMachine = [] {
    std::tm tm{};
    tm.tm_mday = 1;
    x86::X86CmosInputs in{};
    in.calendar = proto::RtcCalendar::of(tm);
    return in;
}();
constexpr x86::X86CmosImage kEmptyCmos = x86::compose_x86_cmos(kEmptyMachine);
static_assert(kEmptyCmos[x86::cmos::kFloppyTypes] == std::byte{0x44});
static_assert(kEmptyCmos[x86::cmos::kYear] == std::byte{0x17});
static_assert(
    kEmptyCmos[x86::cmos::kChecksumLow] == std::byte{0x4F}
        && kEmptyCmos[x86::cmos::kChecksumHigh] == std::byte{0x01},
    "x86.cpp:671-675: 0x44 + 0x4D + 0x80 + 0x02 + 0x3C = 0x14F"
);

}

std::unique_ptr<Core> make_ao486(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<x86::X86Core>(p, g.services, kAo486Family);
}

std::unique_ptr<Core> make_pcxt(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<x86::X86Core>(p, g.services, kPcxtFamily);
}

std::unique_ptr<Core> make_pcjr(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<x86::X86Core>(p, g.services, kPcjrFamily);
}

namespace {

std::unique_ptr<IDiskServant> make_fdc(
    const CoreProfile& p,
    const ServantGrant& g,
    const x86::X86Family& family
) {
    return std::make_unique<x86::FloppyServant>(
        infra::OptRef<const svc::Vfs>{g.vfs},
        p.disk_formats,
        family.drives,
        x86::fdc_params_at(family.poll),
        g.activity
    );
}

std::unique_ptr<IDiskServant> make_bus(
    const CoreProfile& p,
    const ServantGrant& g,
    const x86::X86Family& family
) {
    return std::make_unique<x86::BusServant>(
        infra::OptRef<const svc::Vfs>{g.vfs},
        p.disk_formats,
        family,
        x86::fdc_params_at(family.poll),
        g.activity,
        g.remount
    );
}

}

std::unique_ptr<IDiskServant> make_ao486_bus(const CoreProfile& p, const ServantGrant& g) {
    return make_bus(p, g, kAo486Family);
}

std::unique_ptr<IDiskServant> make_pcxt_bus(const CoreProfile& p, const ServantGrant& g) {
    return make_bus(p, g, kPcxtFamily);
}

std::unique_ptr<IDiskServant> make_pcjr_fdc(const CoreProfile& p, const ServantGrant& g) {
    return make_fdc(p, g, kPcjrFamily);
}

std::unique_ptr<IShareHost> make_ao486_share_host(const svc::Vfs& vfs, std::string_view root) {
    return std::make_unique<x86::ShareHost>(vfs, root);
}

std::unique_ptr<BootLadder> make_ao486_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<x86::X86Ladder>(p, ctx, kAo486Family);
}

std::unique_ptr<BootLadder> make_pcxt_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<x86::X86Ladder>(p, ctx, kPcxtFamily);
}

std::unique_ptr<BootLadder> make_pcjr_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<x86::X86Ladder>(p, ctx, kPcjrFamily);
}

std::unique_ptr<ILoader> make_ao486_loader(const LoaderContext& ctx) {
    return std::make_unique<x86::X86BootLoader>(ctx.vfs, ctx.profile.name);
}

}
