// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/dir_version.h"

#include <utility>

#include "dir_word.h"

namespace mister::proto {

DirVersion DirVersion::of_wire(std::uint32_t word) noexcept {
    DirVersion out;
    out.v = word;
    return out;
}

Error DirVersion::refused(Errc why, infra::ErrorSite at) const noexcept {
    return Error{why, at, v};
}

bool DirVersion::has_major(DirMajor m) const noexcept {
    return DirWord{v}.high16() == std::to_underlying(m);
}

bool DirVersion::is_base_minor() const noexcept {
    return DirWord{v}.low16() == 0;
}

}
