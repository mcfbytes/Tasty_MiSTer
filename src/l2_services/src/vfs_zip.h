// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "browse_rules.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::svc::detail {

struct ZipListing {
    std::vector<ZipMember> members;
};

[[nodiscard]] Ex<ZipListing> zip_list(const std::string& archive);
[[nodiscard]] Ex<std::unique_ptr<IFile>> zip_open_member(
    const std::string& archive,
    const std::string& member
);
[[nodiscard]] Ex<std::unique_ptr<IFile>> zip_open_member_by_crc(
    const std::string& archive,
    Crc32 crc
);

[[nodiscard]] bool zip_archive_opens(const std::string& archive);

[[nodiscard]] Ex<std::unique_ptr<IArchive>> zip_make_archive(
    const std::string& archive,
    std::size_t max
);

}
