// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/vfs.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/byte_view.h"
#include "os/small_file.h"
#include "os/types.h"
#include "os/writeback_paths.h"

namespace mister::os {

inline constexpr FsMagic kCifsMagic{0xFF534D42u};
inline constexpr FsMagic kSmb2Magic{0xFE534D42u};
inline constexpr FsMagic kSmbMagic{0x517Bu};
inline constexpr FsMagic kNfsMagic{0x6969u};

struct WritebackProbe {
    std::optional<FsMagic> fs_magic;
    std::optional<CpuSet> cpumask;
    std::optional<CpuSet> unbound_cpumask;
};

[[nodiscard]] constexpr std::optional<CpuSet> effective_writeback_cpus(
    const WritebackProbe& p
) noexcept {
    if (!p.cpumask || !p.unbound_cpumask)
        return std::nullopt;
    const CpuSet both = p.cpumask->intersect(*p.unbound_cpumask);
    return both.empty() ? *p.unbound_cpumask : both;
}

[[nodiscard]] constexpr std::optional<CpuSet> cpumask_from(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;
    std::uint64_t mask = 0;
    while (true) {
        const auto comma = text.find(',');
        const std::string_view group = text.substr(0, comma);
        std::uint32_t word = 0;
        const auto [end, ec] = std::from_chars(group.data(), group.data() + group.size(), word, 16);
        if (group.empty()
            || group.size() > 8
            || ec != std::errc{}
            || end != group.data() + group.size())
            return std::nullopt;
        mask = (mask << 32) | word;
        if (comma == std::string_view::npos)
            return CpuSet{mask};
        text.remove_prefix(comma + 1);
    }
}

[[nodiscard]] constexpr bool is_network_fs(FsMagic fs_magic) noexcept {
    return fs_magic == kCifsMagic
        || fs_magic == kSmb2Magic
        || fs_magic == kSmbMagic
        || fs_magic == kNfsMagic;
}

[[nodiscard]] inline std::optional<CpuSet> read_cpumask(const char* path) noexcept {
    std::array<char, 512> buf{};
    const auto got = read_small_file(path, std::span<char>(buf));
    if (!got || got->size() >= buf.size())
        return std::nullopt;
    return cpumask_from(infra::as_text(*got));
}

[[nodiscard]] inline WritebackProbe read_writeback_probe(
    const WritebackPaths& paths = {}
) noexcept {
    WritebackProbe p{};
    struct statfs fs{};
    if (::statfs(paths.dir, &fs) == 0)
        p.fs_magic = FsMagic{static_cast<std::uint32_t>(fs.f_type)};
    p.cpumask = read_cpumask(paths.cpumask);
    p.unbound_cpumask = read_cpumask(paths.unbound);
    return p;
}

}
