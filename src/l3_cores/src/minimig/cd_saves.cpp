// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/cd_saves.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "cores/fnv1a64.h"
#include "svc/card_dirs.h"

namespace mister::cores::minimig {

namespace {

struct CdSaveRow {
    CdSave which;
    NvramPolicy policy;
};

constexpr std::array<CdSaveRow, 3> kCdSaveRows{{
    {.which = CdSave::Cd32Nvram,
     .policy =
         {.naming = SaveNaming::PerDiscFnv1a,
          .prefix = "cd32",
          .directory = "Minimig",
          .extension = "nvr"}},
    {.which = CdSave::CdtvNvram,
     .policy =
         {.naming = SaveNaming::PerDiscFnv1a,
          .prefix = "cdtv",
          .directory = "Minimig",
          .extension = "nvr"}},
    {.which = CdSave::CdtvCard,
     .policy = {
         .naming = SaveNaming::PerDiscFnv1a,
         .prefix = "cdtv",
         .directory = "Minimig",
         .extension = "card"
     }},
}};

const NvramPolicy& row_of(CdSave which) noexcept {
    return std::ranges::find(kCdSaveRows, which, &CdSaveRow::which)->policy;
}

constexpr Fnv1a64 kCdtvBasis{1469598103934665603ull};

Fnv1aHex digest_of(CdSave which, std::string_view name) noexcept {
    if (which == CdSave::Cd32Nvram)
        return fnv1a64_hex(name);
    const std::array<char, Fnv1a64::kHexDigits> hex = Fnv1a64::digest_from(kCdtvBasis, name).hex();
    Fnv1aHex out;
    (void)out.assign(std::string_view(hex.data(), hex.size()));
    return out;
}

std::string_view basename_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}

SavePath cd_save_path(CdSave which, std::string_view disc_path) noexcept {
    SavePath out;
    if (disc_path.empty())
        return out;
    const NvramPolicy& p = row_of(which);
    const bool ok = out.assign(svc::kSavesPrefix)
        && out.append(p.directory)
        && out.append("/")
        && out.append(p.prefix)
        && out.append("-")
        && out.append(digest_of(which, basename_of(disc_path)).view())
        && out.append(".")
        && out.append(p.extension);
    if (!ok)
        return SavePath{};
    return out;
}

SavePath cd32_shared_save_path() noexcept {
    SavePath out;
    const NvramPolicy& p = row_of(CdSave::Cd32Nvram);
    (void)(out.assign(svc::kSavesPrefix)
           && out.append(p.directory)
           && out.append("/")
           && out.append(p.prefix)
           && out.append(".")
           && out.append(p.extension));
    return out;
}

}
