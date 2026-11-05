// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "cores/pcm_source.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

[[nodiscard]] std::unique_ptr<IPcmSource> make_pcm_source(
    std::string_view rom_path,
    const svc::Vfs& vfs
);

}
