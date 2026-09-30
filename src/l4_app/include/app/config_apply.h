// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mister::svc {
struct ConfigSnapshot;
class Vfs;
}  // namespace mister::svc

namespace mister::app {

void apply_vga_mode_fixup(svc::ConfigSnapshot& cfg) noexcept;

std::string self_exe_path();

std::optional<std::string> alternate_executable(const svc::Vfs& vfs, std::string_view cfg_main,
                                                std::string_view self_exe);

}  // namespace mister::app
