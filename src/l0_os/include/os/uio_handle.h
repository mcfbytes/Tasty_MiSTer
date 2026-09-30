// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "os/types.h"
#include "infra/seat.h"

namespace mister::os {

[[nodiscard]] int find_uio_node(std::string_view name) noexcept;

[[nodiscard]] int find_uio_node_under(const char* sysfs_root, std::string_view name) noexcept;

class UioHandle {
    TASTY_SEAT_EXEMPT(component);

public:
    UioHandle() = default;

    static Ex<UioHandle> open(UioLine line, const UioLineSpace& space);

    static Ex<UioHandle> adopt(UioLine line, UniqueFd fd);

    int fd() const noexcept { return fd_.get(); }
    UioLine line() const noexcept { return line_; }

    [[nodiscard]] Ex<std::uint32_t> consume();
    [[nodiscard]] Ex<void> rearm();

private:
    UniqueFd fd_;
    UioLine line_{};
};

}  // namespace mister::os
