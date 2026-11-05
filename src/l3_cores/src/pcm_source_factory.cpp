// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pcm_source_factory.h"

#include <memory>
#include <string_view>

#include "cores/mdp_source.h"

namespace mister::cores {

std::unique_ptr<IPcmSource> make_pcm_source(std::string_view rom_path, const svc::Vfs& vfs) {
    if (rom_path.empty())
        return nullptr;
    return std::make_unique<mdp::CueWavSource>(vfs, rom_path);
}

}
