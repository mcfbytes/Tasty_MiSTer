// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "app/remembered_path.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class NameConfig {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kPathBlob = kRememberedBlob;
    static constexpr std::size_t kCidLen = 32;

    using Slot = RememberedSlot;

    explicit NameConfig(const svc::Vfs& vfs, std::string cid_path)
        : vfs_(&vfs), cid_path_(static_cast<std::string&&>(cid_path)) {}

    static std::string card_cid_path(std::string_view sd_block);

    [[nodiscard]] Ex<void> save_path(const RememberedStem& stem, Slot s, std::uint8_t index,
                                     std::string_view path);

    bool scripts_confirm_skippable() const;

    [[nodiscard]] Ex<void> remember_scripts_confirm();

    bool read_card_cid(std::span<std::byte, kCidLen> out) const;

private:
    const svc::Vfs* vfs_;
    std::string cid_path_;
};

}  // namespace mister::app
