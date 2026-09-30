// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/name_config.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "app/durable_write.h"
#include "infra/unique_fd.h"
#include "svc/vfs.h"

namespace mister::app {

std::string NameConfig::card_cid_path(std::string_view sd_block) {
    if (sd_block.empty()) return {};
    std::string p{"/sys/block/"};
    p += sd_block;
    p += "/device/cid";
    return p;
}

std::string NameConfig::blob_name(std::string_view core, Slot s, std::uint8_t index) {
    std::string n{core};
    n += (s == Slot::File) ? ".f" : ".s";
    n += std::to_string(index);
    return n;
}

Ex<void> NameConfig::save_path(std::string_view core, Slot s, std::uint8_t index,
                               std::string_view path) {
    TASTY_SEAT_BODY(NameConfig);
    std::array<std::byte, kPathBlob> blob{};
    std::memcpy(blob.data(), path.data(), std::min(path.size(), kPathBlob - 1));
    return durable_write(*vfs_, "config/" + blob_name(core, s, index), blob);
}

std::string NameConfig::load_path(std::string_view core, Slot s, std::uint8_t index) const {
    TASTY_SEAT_BODY(NameConfig);
    auto f = vfs_->open("config/" + blob_name(core, s, index), svc::OpenMode::ReadWhole);
    if (!f) return {};
    std::array<std::byte, kPathBlob> blob{};
    auto n = (*f)->read_at(0, blob);
    if (!n || *n == 0) return {};
    const char* c = reinterpret_cast<const char*>(blob.data());
    return std::string{c, ::strnlen(c, kPathBlob)};
}

bool NameConfig::read_card_cid(std::span<std::byte, kCidLen> out) const {
    TASTY_SEAT_BODY(NameConfig);
    const UniqueFd fd{::open(cid_path_.c_str(), O_RDONLY | O_CLOEXEC)};
    if (fd.get() < 0) return false;

    return ::read(fd.get(), out.data(), kCidLen) == static_cast<ssize_t>(kCidLen);
}

bool NameConfig::scripts_confirm_skippable() const {
    TASTY_SEAT_BODY(NameConfig);
    std::array<std::byte, kCidLen> live{};
    if (!read_card_cid(live)) return false;
    auto f = vfs_->open("config/script_confirm", svc::OpenMode::ReadWhole);
    if (!f) return false;
    std::array<std::byte, kCidLen> stored{};
    auto n = (*f)->read_at(0, stored);
    if (!n || *n == 0) return false;

    return std::memcmp(live.data(), stored.data(), kCidLen) == 0;
}

Ex<void> NameConfig::remember_scripts_confirm() {
    TASTY_SEAT_BODY(NameConfig);
    std::array<std::byte, kCidLen> live{};
    (void)read_card_cid(live);
    return durable_write(*vfs_, "config/script_confirm", live);
}

}  // namespace mister::app
