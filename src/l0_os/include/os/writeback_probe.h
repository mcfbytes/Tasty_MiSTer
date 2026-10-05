// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/vfs.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "os/small_file.h"

namespace mister::os {

inline constexpr const char* kWritebackCpumaskPath = "/sys/bus/workqueue/devices/writeback/cpumask";

inline constexpr const char* kUnboundCpumaskPath = "/sys/devices/virtual/workqueue/cpumask";

inline constexpr std::uint64_t kCifsMagic = 0xFF534D42u;
inline constexpr std::uint64_t kSmb2Magic = 0xFE534D42u;
inline constexpr std::uint64_t kSmbMagic = 0x517Bu;
inline constexpr std::uint64_t kNfsMagic = 0x6969u;

struct WritebackProbe {
    std::optional<std::uint64_t> fs_magic;
    std::optional<std::uint64_t> cpumask;
    std::optional<std::uint64_t> unbound_cpumask;
};

[[nodiscard]] constexpr std::optional<std::uint64_t> effective_writeback_cpus(
    const WritebackProbe& p) noexcept {
    if (!p.cpumask || !p.unbound_cpumask) return std::nullopt;
    const std::uint64_t both = *p.cpumask & *p.unbound_cpumask;
    return both != 0 ? both : *p.unbound_cpumask;
}

[[nodiscard]] constexpr std::optional<std::uint64_t> cpumask_from(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    std::uint64_t mask = 0;
    while (true) {
        const auto comma = text.find(',');
        const std::string_view group = text.substr(0, comma);
        std::uint32_t word = 0;
        const auto [end, ec] = std::from_chars(group.data(), group.data() + group.size(), word, 16);
        if (group.empty() || group.size() > 8 || ec != std::errc{} ||
            end != group.data() + group.size())
            return std::nullopt;
        mask = (mask << 32) | word;
        if (comma == std::string_view::npos) return mask;
        text.remove_prefix(comma + 1);
    }
}

[[nodiscard]] constexpr bool is_network_fs(std::uint64_t fs_magic) noexcept {
    return fs_magic == kCifsMagic || fs_magic == kSmb2Magic || fs_magic == kSmbMagic ||
           fs_magic == kNfsMagic;
}

[[nodiscard]] inline std::optional<std::uint64_t> read_cpumask(const char* path) noexcept {
    std::array<char, 512> buf{};
    const auto got = read_small_file(path, std::span<char>(buf));
    if (!got || *got >= buf.size()) return std::nullopt;
    return cpumask_from(std::string_view(buf.data(), *got));
}

[[nodiscard]] inline WritebackProbe read_writeback_probe(const char* dir, const char* cpumask_path,
                                                         const char* unbound_path) noexcept {
    WritebackProbe p{};
    struct statfs fs {};
    if (::statfs(dir, &fs) == 0)
        p.fs_magic = static_cast<std::uint64_t>(static_cast<std::uint32_t>(fs.f_type));
    p.cpumask = read_cpumask(cpumask_path);
    p.unbound_cpumask = read_cpumask(unbound_path);
    return p;
}

}  // namespace mister::os
