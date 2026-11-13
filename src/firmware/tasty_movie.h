// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "tasty_exit.h"
#include "tasty_files.h"

namespace mister::svc {
class Vfs;
}

namespace mister::fw {

[[nodiscard]] tasty::Exit tasty_check_movie(const svc::Vfs& vfs, TastyFiles files);

[[nodiscard]] tasty::Exit tasty_info_movie(const svc::Vfs& vfs, std::string_view movie);

}
