// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "app/remembered_path.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app::recents {
class UiWriteLane;
}

namespace mister::app {

class NameConfig {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kPathBlob = kRememberedBlob;
    static constexpr std::size_t kCidLen = 32;

    NameConfig(const svc::Vfs& vfs, app::recents::UiWriteLane& writes, std::string cid_path)
        : vfs_(&vfs),
          writes_(&writes),
          cid_path_(static_cast<std::string&&>(cid_path)) {}

    static std::string card_cid_path(std::string_view sd_block);

    [[nodiscard]] Ex<void> save_path(
        const RememberedStem& stem,
        proto::FileRowId row,
        std::string_view path
    );

    bool scripts_confirm_skippable() const;

    [[nodiscard]] Ex<void> remember_scripts_confirm();

    [[nodiscard]] Ex<std::array<std::byte, kCidLen>> read_card_cid() const;

private:
    const svc::Vfs* vfs_;
    app::recents::UiWriteLane* writes_;
    std::string cid_path_;
};

}
