// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mister::cores {

class IRomsetFiles {
public:
    virtual ~IRomsetFiles() = default;

    [[nodiscard]] virtual std::optional<std::string> read_prefix(std::string_view path,
                                                                 std::uint64_t cap) = 0;

protected:
    IRomsetFiles() = default;
    IRomsetFiles(const IRomsetFiles&) = default;
    IRomsetFiles& operator=(const IRomsetFiles&) = default;
};

}  // namespace mister::cores
