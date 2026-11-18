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
#include <span>
#include <string_view>

#include "app/ui_write_lane.h"
#include "infra/unique_fd.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

constexpr std::string_view kScriptConfirmRel = "config/script_confirm";

}

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
        return std::unexpected(row.index.refusal(Errc::not_found, ERR_SITE()));
    std::array<std::byte, kPathBlob> blob{};
    std::memcpy(blob.data(), path.data(), std::min(path.size(), kPathBlob - 1));
    return writes_->arm("config/" + remembered_blob_name(stem, row), blob);
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
    std::array<std::byte, kCidLen> stored{};

    const auto armed = writes_->peek(kScriptConfirmRel);
    if (!armed)
        return false;
    if (!armed->empty()) {
        const std::span<const std::byte> kept = armed->first(std::min(armed->size(), kCidLen));
        std::copy(kept.begin(), kept.end(), stored.begin());
    } else {
        auto f = vfs_->open(kScriptConfirmRel, svc::OpenMode::ReadWhole);
        if (!f)
            return false;
        auto n = (*f)->read_at(proto::kFileStart, stored);
        if (!n || *n == 0)
            return false;
    }

    return std::memcmp(live->data(), stored.data(), kCidLen) == 0;
}

Ex<void> NameConfig::remember_scripts_confirm() {
    TASTY_SEAT_BODY(NameConfig);

    const std::array<std::byte, kCidLen> live = read_card_cid().value_or(
        std::array<std::byte, kCidLen>{}
    );
    return writes_->arm(kScriptConfirmRel, live);
}

}
