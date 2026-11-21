// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/types.h>

namespace mister::infra {

struct FileMode {
    ::mode_t v = 0;

    [[nodiscard]] constexpr ::mode_t create_mode() const noexcept { return v; }

    friend constexpr bool operator==(FileMode, FileMode) = default;
};

inline constexpr FileMode kPrivateFileMode{0600};

inline constexpr FileMode kSharedFileMode{0644};

inline constexpr FileMode kInheritedDirMode{0777};

inline constexpr FileMode kStandardDirMode{0755};

}
