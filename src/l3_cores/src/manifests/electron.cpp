// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/electron.h"

#include <string_view>

#include "cores/electron_core.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_electron(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<ElectronCore>(p, g.services);
}

static_assert(kElectron.kind == CoreKind::Electron);
static_assert(
    kElectron.name == std::string_view{"AcornElectron"},
    "user_io.cpp:399: is_electron() is strcasecmp(orig_name, \"AcornElectron\") "
    "(req dispatch)"
);
static_assert(
    kElectron.services.empty(),
    "no wire opcodes: the whole machine is the load-time UEF_FileSend transform "
    "(zero rows, verify_census skips an empty-services profile)"
);
static_assert(
    !kElectron.blank_save.declared(),
    "stock Electron has no staging ladder and no blank-fill arm"
);
static_assert(
    kElectron.staging.order == StageOrder::None,
    "no boot ladder: UEF_FileSend runs inline in user_io_file_tx (req's "
    "leaked tmpfile has no host-side mount step to stage)"
);

}
