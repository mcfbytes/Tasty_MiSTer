// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/n64.h"

#include <string_view>

#include "cores/n64_core.h"
#include "cores/n64_loader.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_n64(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<N64Core>(p, g.services);
}

std::unique_ptr<ILoader> make_n64_loader(const LoaderContext& ctx) {
    return std::make_unique<N64Loader>(ctx.vfs, ctx.profile.name, ctx.turbo, ctx.memo);
}

static_assert(kN64.kind == CoreKind::N64);
static_assert(
    kN64.name == std::string_view{"N64"},
    "n64.cpp is reached through is_n64(), user_io.cpp's strcasecmp against \"N64\""
);
static_assert(
    kN64.services[0].round_claim == RoundClaim::Always,
    "the cheat tick claims each round it runs (no OSD row)"
);
static_assert(
    kN64.services.size() == 1,
    "ONE census row: the cheat engine, which is the whole of n64_poll that "
    "is not 64DD"
);
static_assert(
    kN64.services[0].period_ms == 16,
    "the NTSC frame on the executive's 1 kHz grid; the 17/17/16 and PAL-20 "
    "gaps accumulate inside the body, because a row period equal to the gap "
    "would alias every 17 ms deadline onto the round after next"
);
static_assert(
    kN64.staging.order == StageOrder::None,
    "no boot ladder: the saves are mounted after the ROM tx, not stepped"
);
static_assert(
    !kN64.blank_save.declared(),
    "the Controller Pak blank is a RENDER, not a constexpr pattern: n64_cpak's "
    "sixteen serial bytes come from /dev/urandom at mount, and a declarable "
    "pattern would make every pak on every card the same one"
);
static_assert(
    kN64.windows.size() == 1 && !kN64.windows[0].feeds_pcm,
    "ONE declared window, the RDRAM view, and nothing feeds it audio"
);
static_assert(
    kN64.analog_reshape.has_value() && kN64.analog_reshape.operator->() == &n64::kJoyEmu,
    "the octagon is manifest data; L2 reaches it through this row alone"
);
static_assert(kN64Slots.size() == 4, "the shipping CONF_STR publishes four F items");
static_assert(
    kN64.host.pause == PauseOnOsd::Always,
    "the N64 RTL pauses on OSD_STATUS with no option, so the badge reads the level "
    "alone (N64.sv:795); an optionless core takes Option"
);

}
