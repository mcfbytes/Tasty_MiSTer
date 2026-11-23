// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/zx81.h"

#include <string_view>

#include "cores/generic_profile.h"
#include "cores/zx81/zx81_sidecars.h"
#include "infra/ascii.h"

namespace mister::cores::manifests {

std::unique_ptr<ISidecarBuilder> make_zx81_sidecars() {
    return std::make_unique<zx81::Zx81Sidecars>();
}

static_assert(kZx81.kind == CoreKind::Zx81);
static_assert(
    infra::ieq(kZx81.name, "zx81"),
    "user_io.cpp:265: is_zx81() is strcasecmp(orig_name, \"zx81\")"
);
static_assert(
    kZx81.services.empty()
        && kZx81.slots.empty()
        && kZx81.boot_assets.empty()
        && kZx81.start_assets.empty(),
    "user_io.cpp has one is_zx81() arm (:2945): no opcode, slot or boot row of its own"
);
static_assert(
    kZx81.start == kGenericProfile.start
        && kZx81.undeclared_role == kGenericProfile.undeclared_role
        && kZx81.undeclared_start_mount == kGenericProfile.undeclared_start_mount,
    "outside :2945 the core takes every generic arm: the start chain and S-slot route"
);

}
