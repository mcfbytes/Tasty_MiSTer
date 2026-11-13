// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/name_config.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "app/durable_write.h"
#include "infra/unique_fd.h"
#include "svc/vfs.h"

namespace mister::app {

std::string NameConfig::card_cid_path(std::string_view sd_block) {
    if (sd_block.empty())
        return {};
    std::string p{"/sys/block/"};
    p += sd_block;
    p += "/device/cid";
    return p;
}

Ex<void> NameConfig::save_path(
    const RememberedStem& stem,
    proto::FileRowId row,
    std::string_view path
) {
    TASTY_SEAT_BODY(NameConfig);

    if (stem.empty())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), row.index.v});
    std::array<std::byte, kPathBlob> blob{};
    std::memcpy(blob.data(), path.data(), std::min(path.size(), kPathBlob - 1));
    return durable_write(*vfs_, "config/" + remembered_blob_name(stem, row), blob);
}

Ex<std::array<std::byte, NameConfig::kCidLen>> NameConfig::read_card_cid() const {
    TASTY_SEAT_BODY(NameConfig);
    std::array<std::byte, kCidLen> out{};
    const UniqueFd fd{::open(cid_path_.c_str(), O_RDONLY | O_CLOEXEC)};
    if (fd.get() < 0)
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), errno));

    const ssize_t got = ::read(fd.get(), out.data(), kCidLen);
    if (got < 0)
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), errno));
    if (got != std::ssize(out)) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(got)}
        );
    }
    return out;
}

bool NameConfig::scripts_confirm_skippable() const {
    TASTY_SEAT_BODY(NameConfig);
    const auto live = read_card_cid();
    if (!live)
        return false;
    auto f = vfs_->open("config/script_confirm", svc::OpenMode::ReadWhole);
    if (!f)
        return false;
    std::array<std::byte, kCidLen> stored{};
    auto n = (*f)->read_at(0, stored);
    if (!n || *n == 0)
        return false;

    return std::memcmp(live->data(), stored.data(), kCidLen) == 0;
}

Ex<void> NameConfig::remember_scripts_confirm() {
    TASTY_SEAT_BODY(NameConfig);

    const std::array<std::byte, kCidLen> live = read_card_cid().value_or(
        std::array<std::byte, kCidLen>{}
    );
    return durable_write(*vfs_, "config/script_confirm", live);
}

}
