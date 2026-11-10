// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/config_apply.h"

#include "app/types.h"
#include "infra/ascii.h"

#include "svc/config_snapshot.h"
#include "svc/vfs.h"

#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <utility>

namespace mister::app {

void apply_vga_mode_fixup(svc::ConfigSnapshot& cfg) noexcept {
    const std::string_view v(cfg.vga_mode);
    if (v.empty())
        return;
    if (infra::ieq(v, "rgb"))
        cfg.vga_mode_int = wire_of(VgaMode::Rgb);
    if (infra::ieq(v, "ypbpr"))
        cfg.vga_mode_int = wire_of(VgaMode::YPbPr);
    if (infra::ieq(v, "svideo"))
        cfg.vga_mode_int = wire_of(VgaMode::SVideo);
    if (infra::ieq(v, "cvbs"))
        cfg.vga_mode_int = wire_of(VgaMode::Cvbs);
    if (infra::ieq(v, "subcarrier")) {
        cfg.vga_mode_int = wire_of(VgaMode::Subcarrier);
        cfg.csync = 1;
        cfg.forced_scandoubler = 0;
    }
}

std::string self_exe_path() {
    char buf[PATH_MAX] = {};
    const ::ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    return std::string(buf, static_cast<std::size_t>(n));
}

std::optional<std::string> alternate_executable(
    const svc::Vfs& vfs,
    std::string_view cfg_main,
    std::string_view self_exe
) {
    if (cfg_main.empty() || self_exe.empty())
        return std::nullopt;

    const svc::SearchPolicy policy{svc::search::kRootOnly, true};
    auto resolved = vfs.resolve(cfg_main, policy);
    if (!resolved)
        return std::nullopt;

    struct ::stat st = {};
    if (::stat(resolved->c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return std::nullopt;
    }

    if (infra::ieq(*resolved, self_exe))
        return std::nullopt;

    char real[PATH_MAX] = {};
    if (::realpath(resolved->c_str(), real) != nullptr
        && infra::ieq(std::string_view(real), self_exe)) {
        return std::nullopt;
    }
    return std::optional<std::string>(std::move(*resolved));
}

}
