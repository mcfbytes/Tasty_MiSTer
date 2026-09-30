// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class NameConfig {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kPathBlob = 1024;
    static constexpr std::size_t kCidLen = 32;

    enum class Slot : std::uint8_t { File, Mount };

    explicit NameConfig(const svc::Vfs& vfs, std::string cid_path)
        : vfs_(&vfs), cid_path_(static_cast<std::string&&>(cid_path)) {}

    static std::string card_cid_path(std::string_view sd_block);

    [[nodiscard]] Ex<void> save_path(std::string_view core, Slot s, std::uint8_t index,
                                     std::string_view path);

    std::string load_path(std::string_view core, Slot s, std::uint8_t index) const;

    bool scripts_confirm_skippable() const;

    [[nodiscard]] Ex<void> remember_scripts_confirm();

    static std::string blob_name(std::string_view core, Slot s, std::uint8_t index);

    bool read_card_cid(std::span<std::byte, kCidLen> out) const;

private:
    const svc::Vfs* vfs_;
    std::string cid_path_;
};

}  // namespace mister::app
