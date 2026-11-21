// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/file_create.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>

namespace mister::os {

std::expected<infra::UniqueFd, infra::Errno> open_existing(
    const char* path,
    OpenFlags flags
) noexcept {
    const int fd = ::open(path, flags.open_word());
    if (fd < 0)
        return std::unexpected(infra::Errno{errno});
    return infra::UniqueFd{fd};
}

std::expected<infra::UniqueFd, infra::Errno> open_created(
    const char* path,
    OpenFlags flags,
    infra::FileMode mode
) noexcept {
    const int fd = ::open(path, flags.open_word(), mode.create_mode());
    if (fd < 0)
        return std::unexpected(infra::Errno{errno});
    return infra::UniqueFd{fd};
}

std::expected<DirMade, infra::Errno> make_dir(const char* path, infra::FileMode mode) noexcept {
    if (::mkdir(path, mode.create_mode()) == 0)
        return DirMade::Created;
    if (errno == EEXIST)
        return DirMade::Existed;
    return std::unexpected(infra::Errno{errno});
}

}
