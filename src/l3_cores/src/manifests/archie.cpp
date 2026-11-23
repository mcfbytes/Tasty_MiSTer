// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/archie.h"

#include <array>
#include <cstddef>
#include <memory>

#include "cores/archie/archie_config.h"
#include "cores/archie/archie_ladder.h"
#include "cores/archie/kart_axis.h"
#include "cores/archie/kart_byte.h"
#include "cores/archie/kart_controller.h"
#include "cores/archie/kart_key_map.h"
#include "proto/bit_field.h"
#include "proto/mouse_delta.h"
#include "proto/status_bit.h"

namespace mister::cores::manifests {

namespace {

using archie::ArchieConfig;
using archie::ArchieCore;
using archie::ArchieOption;
using archie::KartByte;
using archie::KartController;
using archie::SystemCtrl;

[[nodiscard]] consteval proto::BitField status_of(ArchieOption o) {
    return SystemCtrl::home_of(o).value().status;
}

[[nodiscard]] consteval proto::BitField file_of(ArchieOption o) {
    return SystemCtrl::home_of(o).value().file;
}

static_assert(ArchieConfig::kSystemCtrlAt == infra::ByteOffset{0}, "archie_config_t::system_ctrl");
static_assert(ArchieConfig::kRomImgAt == infra::ByteOffset{4}, "archie_config_t::rom_img");
static_assert(ArchieConfig::kHddImgAt == infra::ByteOffset{1028}, "archie_config_t::hdd_img[0]");
static_assert(
    ArchieConfig::kHddImgAt + infra::ByteCount{ArchieConfig::kPathBytes} == infra::ByteOffset{2052},
    "archie_config_t::hdd_img[1]"
);
static_assert(ArchieConfig::kBytes == 3076, "sizeof(archie_config_t), archie_init's size test");

static_assert(file_of(ArchieOption::Aspect) == proto::BitField{proto::StatusBit{6}, 2});
static_assert(status_of(ArchieOption::Aspect) == proto::BitField{proto::StatusBit{6}, 2});

static_assert(file_of(ArchieOption::Scale) == proto::BitField{proto::StatusBit{8}, 2});
static_assert(status_of(ArchieOption::Scale) == proto::BitField{proto::StatusBit{8}, 2});

static_assert(file_of(ArchieOption::Refresh) == proto::BitField{proto::StatusBit{4}, 1});
static_assert(status_of(ArchieOption::Refresh) == proto::BitField{proto::StatusBit{4}, 1});

static_assert(file_of(ArchieOption::AudioFix) == proto::BitField{proto::StatusBit{5}, 1});
static_assert(status_of(ArchieOption::AudioFix) == proto::BitField{proto::StatusBit{5}, 1});

static_assert(file_of(ArchieOption::StereoMix) == proto::BitField{proto::StatusBit{1}, 2});
static_assert(status_of(ArchieOption::StereoMix) == proto::BitField{proto::StatusBit{2}, 2});
static_assert(
    SystemCtrl{0b110}.get(SystemCtrl::home_of(ArchieOption::StereoMix).value())
        == proto::FieldValue{3},
    "archie_get_amix: (system_ctrl >> 1) & 3"
);
static_assert(
    SystemCtrl{}.with(SystemCtrl::home_of(ArchieOption::Scale).value(), proto::FieldValue{2})
        == SystemCtrl{0x200},
    "archie_set_scale: (i & 3) << 8"
);

static_assert(ArchieCore::kRomIndex == IoIndex{1});
static_assert(archie::ArchieLadder::kDefaultRom.name == "RISCOS.ROM", "archie_init's default");

static_assert(ArchieCore::kCmosIndex == IoIndex{3});
static_assert(ArchieCore::kCmos.bytes == infra::ByteCount{256} && CmosLayout::kClockBytes == 16);
static_assert(
    ArchieCore::kCmos.year_at == infra::ByteOffset{0xC0}
    && ArchieCore::kCmos.century_at == infra::ByteOffset{0xC1}
);
static_assert(ArchieCore::kCmos.century == std::byte{20});
static_assert(ArchieCore::kCmosFile == "cmos.dat", "cmos_name");

static_assert(
    KartController::kHrst == KartByte{0xFF}
    && KartController::kRak1 == KartByte{0xFE}
    && KartController::kRak2 == KartByte{0xFD}
);
static_assert(
    KartController::kRqid == KartByte{0x20} && KartController::kKbid == KartByte{0x80 | 1}
);
static_assert(
    KartController::kKeyDown == KartByte{0xC0} && KartController::kKeyUp == KartByte{0xD0}
);
static_assert(
    KartController::kBack == KartByte{0x3F}
    && KartController::kNack == KartByte{0x30}
    && KartController::kSack == KartByte{0x31}
    && KartController::kMack == KartByte{0x32}
    && KartController::kSmak == KartByte{0x33}
);
static_assert(
    KartController::kQueueSlots == 8 && KartController::kQueueDepth == 7,
    "QUEUE_LEN: a ring of eight holds seven"
);
static_assert(KartController::kAckTimeout == std::chrono::milliseconds{10}, "archie_kbd_tx");
static_assert(KartController::kResetTimeout == std::chrono::milliseconds{20}, "archie_init");
static_assert(
    archie::KartAxis::kMin == archie::KartAxis{-64}
        && archie::KartAxis::kMax == archie::KartAxis{63},
    "archie_mouse's clamp"
);
static_assert(
    archie::KartAxis{-1}.byte() == KartByte{0x7F}
        && archie::KartAxis{-64}.byte() == KartByte{0x40}
        && archie::KartAxis{63}.byte() == KartByte{0x3F},
    "archie_mouse: delta & 0x7f"
);
static_assert(
    archie::KartAxis{60}.plus(archie::KartAxis{9}) == archie::KartAxis{63}
        && archie::KartAxis::y_of(proto::MouseDelta{.dy = 5}) == archie::KartAxis{-5},
    "archie_mouse: the clamp, and Y inverted"
);

static_assert(archie::kKartKeyRows.size() == 101);
static_assert(archie::kart_key_of(svc::EvdevCode{1}) == archie::kKartEscape);
static_assert(archie::kart_key_of(svc::EvdevCode{2}) == archie::kKartKey1);
static_assert(archie::kart_key_of(svc::EvdevCode{3}) == archie::kKartKey2);
static_assert(archie::kart_key_of(svc::EvdevCode{4}) == archie::kKartKey3);
static_assert(archie::kart_key_of(svc::EvdevCode{28}) == archie::KartKey{0x47}, "KEY_ENTER");
static_assert(archie::kart_key_of(svc::EvdevCode{57}) == archie::KartKey{0x5F}, "KEY_SPACE");
static_assert(archie::kart_key_of(svc::EvdevCode{127}) == archie::KartKey{0x71}, "KEY_COMPOSE");
static_assert(!archie::kart_key_of(svc::EvdevCode{70}), "KEY_SCROLLLOCK presses nothing");
static_assert(
    !archie::kart_key_of(svc::EvdevCode{125}) && !archie::kart_key_of(svc::EvdevCode{126}),
    "the GUI keys are the chord, never a key"
);
static_assert(
    archie::kKartBreak == archie::KartKey{0x0F}
    && archie::kKartMouseExtra1 == archie::KartKey{0x73}
    && archie::kKartMouseExtra2 == archie::KartKey{0x74}
    && archie::kKartKeypadHash == archie::KartKey{0x25}
);
static_assert(
    archie::kKartMouseButtons
        == std::array{archie::KartKey{0x70}, archie::KartKey{0x72}, archie::KartKey{0x71}},
    "archie_mouse: prefix | 0x07, then remap[] = {0, 2, 1}"
);
static_assert(
    archie::KartKey{0x3C}.event(KartController::kKeyDown)
        == std::array{KartByte{0xC3}, KartByte{0xCC}},
    "archie_kbd: prefix | (code >> 4), then prefix | (code & 0x0f)"
);

static_assert(kArchie.suppresses_mgl && kArchie.host.forces_gui_f12);
static_assert(
    kArchie.settings_in_startup_slot && kArchie.config_codec,
    "menu.cpp:3073-3077: Save settings writes the config file and nothing else"
);
static_assert(kArchie.reset.reload_hold == std::chrono::milliseconds{1000});
static_assert(!kArchie.reset.button_reacts, "a short press is the fabric's own reset");
static_assert(kArchieOptionRows.size() == 11);
static_assert(kArchieSlots[0].extensions == "ADF" && kArchieSlots[1].extensions == "ADF");

}

std::unique_ptr<Core> make_archie(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<archie::ArchieCore>(p, g.services);
}

std::unique_ptr<BootLadder> make_archie_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<archie::ArchieLadder>(p, ctx);
}

}
