// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/snes.h"

#include <string_view>

#include "cores/snes_core.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_snes(const CoreProfile& p, const HostServices& h) {
    return std::make_unique<SnesCore>(p, h);
}

static_assert(kSnes.kind == CoreKind::Snes);
static_assert(kSnes.name == std::string_view{"SNES"},
              "user_io.cpp:244: is_snes() is strcasecmp(orig_name, \"SNES\")");
static_assert(kSnes.services.empty(), "the MSU-1 poll machine is a later pass: zero rows, and "
                                      "verify_census skips an empty-services profile");
static_assert(!kSnes.blank_save.declared(),
              "user_io.cpp:3474-3477: stock SNES has no blank-fill arm; a "
              "fresh save reads back the generic 0xFF memset");
static_assert(kSnes.staging.order == StageOrder::None,
              "no boot ladder: the save mount is the host-side opensave act "
              "inside the ROM tx, not a staging rung");

}  // namespace mister::cores::manifests
