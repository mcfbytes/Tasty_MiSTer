// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/romset_alias_reader.h"

#include <cstddef>
#include <vector>

#include "cores/file_slot.h"
#include "cores/romset_alias_source.h"
#include "svc/read_bounded.h"
#include "svc/vfs.h"

namespace mister::app {

RomsetAliasReader::RomsetAliasReader(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

RomsetAliasReader::~RomsetAliasReader() = default;

void RomsetAliasReader::bind(const cores::FileSlot& slot) {
    TASTY_SEAT_BODY(RomsetAliasReader);
    source_ = (slot.romset_aliases != nullptr) ? slot.romset_aliases() : nullptr;
}

std::optional<std::string> RomsetAliasReader::read_prefix(std::string_view path,
                                                          std::uint64_t cap) {
    TASTY_SEAT_BODY(RomsetAliasReader);
    auto bytes = svc::read_bounded(*vfs_, path, cap);
    if (!bytes) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

RomsetAlias RomsetAliasReader::query(std::string_view dir, std::string_view home,
                                     std::string_view entry, std::string_view key) {
    TASTY_SEAT_BODY(RomsetAliasReader);
    if (!source_) return {};
    return source_->query(*this, dir, home, entry, key);
}

}  // namespace mister::app
