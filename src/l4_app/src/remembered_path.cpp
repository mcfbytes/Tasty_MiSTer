// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/remembered_path.h"

#include <array>
#include <cstring>

#include "svc/vfs.h"

namespace mister::app {

std::string remembered_blob_name(const RememberedStem& stem, RememberedSlot s, std::uint8_t index) {
    std::string n{stem.view()};
    n += (s == RememberedSlot::File) ? ".f" : ".s";
    n += std::to_string(index);
    return n;
}

std::string read_remembered_path(const svc::Vfs& vfs, const RememberedStem& stem, RememberedSlot s,
                                 std::uint8_t index) {
    if (stem.empty()) return {};
    auto f = vfs.open("config/" + remembered_blob_name(stem, s, index), svc::OpenMode::ReadWhole);
    if (!f) return {};
    std::array<std::byte, kRememberedBlob> blob{};
    auto n = (*f)->read_at(0, blob);
    if (!n || *n == 0) return {};
    const char* c = reinterpret_cast<const char*>(blob.data());
    return std::string{c, ::strnlen(c, kRememberedBlob)};
}

}  // namespace mister::app
