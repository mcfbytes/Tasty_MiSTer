// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/save_state_name.h"

#include <cstddef>
#include <utility>

#include "infra/path_parts.h"

namespace mister::proto {

namespace {

enum : std::size_t { kPathSlack = 8 };

}

std::string savestate_stem(std::string_view rom_path) {
    return std::string(infra::stem(rom_path));
}

std::optional<SaveStateName> SaveStateName::of(const SaveTarget& target) {
    const std::string_view dir = target.arcade ? kSaveStateArcadeSubdir : target.core_name;
    std::string basename = savestate_stem(target.rom_path);
    if (dir.empty() || basename.empty())
        return std::nullopt;
    return SaveStateName{std::string(dir), std::move(basename)};
}

std::string SaveStateName::dir() const {
    std::string out;
    out.reserve(kSaveStateRoot.size() + subdir.size() + 1u);
    out.append(kSaveStateRoot);
    out.push_back('/');
    out.append(subdir);
    return out;
}

std::string SaveStateName::path(StateSlot slot) const {
    std::string out;
    out.reserve(kSaveStateRoot.size() + subdir.size() + stem.size() + kPathSlack);
    out.append(kSaveStateRoot);
    out.push_back('/');
    out.append(subdir);
    out.push_back('/');
    out.append(stem);
    out.push_back('_');
    out.append(std::to_string(slot.digit()));
    out.append(kSaveStateExt);
    return out;
}

std::string SaveStateName::stock_path() const {
    std::string out;
    out.reserve(kSaveStateRoot.size() + subdir.size() + stem.size() + kPathSlack);
    out.append(kSaveStateRoot);
    out.push_back('/');
    out.append(subdir);
    out.push_back('/');
    out.append(stem);
    out.append(kSaveStateExt);
    return out;
}

}
