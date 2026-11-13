// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/socket_group.h"

#include <grp.h>

#include <array>
#include <cstring>

namespace mister::os {

SocketGroup SocketGroup::named(std::string_view group) noexcept {
    constexpr std::size_t kGroupNameBuf = 32;
    constexpr std::size_t kGroupEntryBuf = 4096;
    if (group.empty() || group.size() >= kGroupNameBuf)
        return none();
    std::array<char, kGroupNameBuf> name{};
    std::memcpy(name.data(), group.data(), group.size());
    struct group entry{};
    std::array<char, kGroupEntryBuf> buf{};
    struct group* found = nullptr;
    if (::getgrnam_r(name.data(), &entry, buf.data(), buf.size(), &found) != 0
        || found == nullptr) {
        return none();
    }
    return SocketGroup{GroupId::of(found->gr_gid), kModeNamed};
}

}
