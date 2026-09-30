// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

namespace mister::os {

inline constexpr const char* kCompactUnevictablePath = "/proc/sys/vm/compact_unevictable_allowed";
inline constexpr const char* kCompactionProactivenessPath = "/proc/sys/vm/compaction_proactiveness";

struct VmCompaction {
    std::optional<long> unevictable_allowed;
    std::optional<long> proactiveness;
};

[[nodiscard]] std::optional<long> sysctl_value_from(std::string_view text) noexcept;

[[nodiscard]] VmCompaction read_vm_compaction(const char* unevictable_path,
                                              const char* proactiveness_path) noexcept;

}  // namespace mister::os
