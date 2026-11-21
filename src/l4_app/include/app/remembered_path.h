// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/core_name.h"
#include "app/mra_facts.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

inline constexpr std::size_t kRememberedBlob = 1024;

class RememberedStem {

public:
    constexpr RememberedStem() noexcept = default;

    [[nodiscard]] static RememberedStem of(
        std::string_view conf_str_name,
        const MraFacts& facts
    ) noexcept {
        RememberedStem s;
        (void)s.text_.assign(effective_name(conf_str_name, facts));
        return s;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return text_.view(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return text_.empty(); }

    friend constexpr bool operator==(const RememberedStem&, const RememberedStem&) noexcept =
        default;

private:
    CoreName text_{};
};

[[nodiscard]] std::string remembered_blob_name(const RememberedStem& stem, proto::FileRowId row);

[[nodiscard]] std::string read_remembered_path(
    const svc::Vfs& vfs,
    const RememberedStem& stem,
    proto::FileRowId row
);

}
