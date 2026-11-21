// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/dir_version.h"

#include <utility>

#include "proto/link/dir_word.h"
#include "infra/prelude.h"

namespace mister::proto::link {

DirVersion DirVersion::of_wire(hal::RegWord word) noexcept {
    DirVersion out;
    out.v = word.bits();
    return out;
}

Error DirVersion::refusal(Errc why, infra::ErrorSite at) const noexcept {
    return Error{why, at, v};
}

bool DirVersion::has_major(DirMajor m) const noexcept {
    return DirWord{v}.high16() == std::to_underlying(m);
}

bool DirVersion::is_base_minor() const noexcept {
    return DirWord{v}.low16() == 0;
}

}
