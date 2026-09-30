// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/vm_compaction.h"

#include <array>
#include <charconv>
#include <span>

#include "os/small_file.h"

namespace mister::os {
namespace {

std::optional<long> read_one(const char* path) noexcept {
    std::array<char, 32> buf{};
    const auto got = read_small_file(path, std::span<char>(buf));
    if (!got) return std::nullopt;
    return sysctl_value_from(std::string_view(buf.data(), *got));
}

}  // namespace

std::optional<long> sysctl_value_from(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.remove_suffix(1);
    long v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return v;
}

VmCompaction read_vm_compaction(const char* unevictable_path,
                                const char* proactiveness_path) noexcept {
    return VmCompaction{read_one(unevictable_path), read_one(proactiveness_path)};
}

}  // namespace mister::os
