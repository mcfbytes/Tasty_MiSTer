// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_ladder.h"
#include "cores/manifests/psx.h"

#include <string_view>

#include "cores/psx_core.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_psx(const CoreProfile& p, const HostServices& h) {
    return std::make_unique<PsxCore>(p, kPsxCuePolicy, h);
}

static_assert(kPsxMcdHeader.size() == 8192, "mcdheader.h:2: static const uint8_t mcdheader[8192]");
static_assert(kPsxMcdHeader[0x0000] == std::byte{'M'} && kPsxMcdHeader[0x0001] == std::byte{'C'},
              "mcdheader.h: frame 0 opens with the MC magic");
static_assert(kPsxMcdHeader[0x007F] == std::byte{0x0E},
              "mcdheader.h: frame 0 closes with XOR checksum 0x0E ('M' ^ 'C')");
static_assert(kPsxMcdHeader[0x0080] == std::byte{0xA0} &&
                  kPsxMcdHeader[0x0088] == std::byte{0xFF} &&
                  kPsxMcdHeader[0x0089] == std::byte{0xFF} &&
                  kPsxMcdHeader[0x00FF] == std::byte{0xA0},
              "mcdheader.h: frames 1-15 are free directory entries — 0xA0, "
              "next-block 0xFFFF, checksum 0xA0");
static_assert(kPsxMcdHeader[0x0800] == std::byte{0xFF} &&
                  kPsxMcdHeader[0x0803] == std::byte{0xFF} &&
                  kPsxMcdHeader[0x087F] == std::byte{0x00},
              "mcdheader.h: frames 16-35 are broken-sector list terminators "
              "with checksum 0x00");
static_assert(kPsxMcdHeader[0x1F80] == std::byte{'M'} && kPsxMcdHeader[0x1FFF] == std::byte{0x0E},
              "mcdheader.h: frame 63 (test-write frame) mirrors frame 0");

consteval int psx_mcd_nonzero_bytes() {
    int n = 0;
    for (std::byte b : kPsxMcdHeader) {
        if (b != std::byte{0}) ++n;
    }
    return n;
}
static_assert(psx_mcd_nonzero_bytes() == 186,
              "mcdheader.h: 186 nonzero bytes in 73 runs; check-blanksave.sh "
              "joins the full image against the classic text");

static_assert(kPsxBlankSave.fill == std::byte{0x00},
              "psx.cpp:458: beyond the header, memset(buffer, 0, size)");
static_assert(kPsxBlankSave.declared(), "a declared pattern, never the generic 0xFF fallback");
static_assert(kPsxBlankSave.header.size() == 8192 && kPsxBlankSave.regions.empty(),
              "psx.cpp:452-455: the whole mcdheader lands at image offset 0");

static_assert(kPsx.kind == CoreKind::Psx);
static_assert(kPsx.name == std::string_view{"PSX"},
              "user_io.cpp:373: is_psx() is strcasecmp(orig_name, \"PSX\")");

static_assert(kPsx.services.size() == 1 && &kPsx.services[0] == &kPsxSectorKick,
              "one census obligation: the SectorServed kick, nothing else");
static_assert(kPsxSectorKick.cause == reactor::Cause::Tick && kPsxSectorKick.period_ms == 0,
              "psx.cpp:804-807 + user_io.cpp:3825: one bare kick per poll "
              "round; tasty binds it to every executive round");
static_assert(kPsxSectorKick.osd_budget == OsdBudget::Shared,
              "an every-round row never pins the OSD budget");
static_assert(kPsxSectorKick.impl == &kSectorKick<kPsx>,
              "the shared SectorServed body, identity-keyed to THIS profile");
static_assert(kPsx.rbf.empty(), "support/psx carries no rbf-stem spelling to transcribe");

static_assert(kPsx.slots.size() == 3);
static_assert(kPsx.slots[0].index == IoIndex{1} && !kPsx.slots[0].writable &&
                  kPsx.slots[0].extensions == std::string_view{"CUECHD"},
              "captured CONF_STR: H7S1,CUECHD,Load CD");
static_assert(kPsx.slots[1].index == IoIndex{2} && kPsx.slots[1].writable &&
                  kPsx.slots[2].index == IoIndex{3} && kPsx.slots[2].writable &&
                  kPsx.slots[1].extensions == std::string_view{"SAVMCD"},
              "captured CONF_STR: SC2/SC3,SAVMCD — the two memory cards");
static_assert(kPsx.slots[0].role == SlotRole::Disc && kPsx.slots[1].role == SlotRole::Save &&
                  kPsx.slots[2].role == SlotRole::Save,
              "menu.cpp:2814 ladders index 1 only; a memcard takes the generic mount");

static_assert(kPsx.staging.order == StageOrder::Psx && kPsx.staging.disc_slot == IoIndex{1} &&
                  kPsx.staging.save_slot == IoIndex{2},
              "user_io.cpp:3251 slot 1 disc; psx.cpp:433 memcard on slot 2");
static_assert(kPsx.staging.reset_pulse_us == 0,
              "psx.cpp has no reset pulse: the reset request rides disc_t "
              "metadata bit 2, not status bit 0");

static_assert(kPsxBootAssets.size() == 2,
              "psx.cpp:732-756: cd_bios.rom from the game dir, then its parent");
static_assert(kPsxBootAssets[0].anchor == AssetAnchor::ImageDir &&
                  kPsxBootAssets[1].anchor == AssetAnchor::ImageParentDir,
              "psx.cpp:736-747: <dir>/cd_bios.rom, then <parent>/cd_bios.rom");
static_assert(kPsxBootAssets[0].dest == WideIoIndex{0xC0} &&
                  kPsxBootAssets[1].dest == WideIoIndex{0xC0},
              "psx.cpp:681: user_io_file_tx(filename, 0xC0) — (3<<6)|0");
static_assert(kPsxBootAssets[0].group == kPsxBootAssets[1].group &&
                  !kPsxBootAssets[0].group_required && !kPsxBootAssets[1].group_required &&
                  kPsxBootAssets[0].optional && kPsxBootAssets[1].optional,
              "psx.cpp:732-747: one alternatives group, first hit wins, and a "
              "missing BIOS does not end the ladder");
static_assert(kPsxBootAssets[0].loader == AssetLoader::GenericFileTx &&
                  kPsxBootAssets[1].loader == AssetLoader::GenericFileTx,
              "psx.cpp:681: the generic file-tx arm, not the PCE loader");
static_assert(kPsxBootAssets[0].exact_size == 512 * 1024 &&
                  kPsxBootAssets[1].exact_size == 512 * 1024,
              "psx.cpp:680: `if (sz != 512*1024) return 0` — wrong size means "
              "no wire act and the next candidate is tried");

static_assert(kPsxCuePolicy.track_close == svc::CuePolicy::TrackClose::ProvisionalInclusive,
              "psx.cpp:315: end = table->end - 1, re-derived as each track lands");
static_assert(kPsxCuePolicy.pregap_model == svc::CuePolicy::PregapModel::IndexTriple,
              "psx.cpp:164-331: pregap accumulator + per-track flag + indexes[1]");
static_assert(kPsxCuePolicy.post_load_shift ==
                  svc::CuePolicy::PostLoadTocShift::FakeLeadInPregap150,
              "psx.cpp:136-156: CHD track 0 forced to a 150-sector lead-in, "
              "later tracks re-chained start = prev.end + 1");
static_assert(svc::unimplemented_axis(kPsxCuePolicy) == svc::CueAxis::None,
              "phase B stage 2: the triple is a built program — and still "
              "unreachable in the shipping binary until the registry row lands");
static_assert(kPsxCuePolicy.sub_naming == svc::CuePolicy::SubNaming::None,
              "support/psx: no .sub sidecar arm exists in this family");

std::unique_ptr<BootLadder> make_psx_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<PsxLadder>(p, ctx);
}

}  // namespace mister::cores::manifests
