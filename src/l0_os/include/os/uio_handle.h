// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "os/types.h"
#include "infra/seat.h"
#include "os/uio_line_ref.h"

namespace mister::os {

inline constexpr const char* kUioClassRoot = "/sys/class/uio";

[[nodiscard]] int find_uio_node(std::string_view name) noexcept;

[[nodiscard]] int find_uio_node_under(const char* sysfs_root, std::string_view name) noexcept;

class UioHandle {
    TASTY_SEAT_EXEMPT(component);

public:
    UioHandle() = default;

    [[nodiscard]] static Ex<UioHandle> open(UioLine line, const UioLineSpace& space);

    [[nodiscard]] static Ex<UioHandle> adopt(UioLine line, UniqueFd fd);

    [[nodiscard]] UioLineRef borrow() const noexcept {
        return UioLineRef{fd_.valid() ? fd_.get() : -1, line_};
    }

    int fd() const noexcept { return fd_.valid() ? fd_.get() : -1; }
    UioLine line() const noexcept { return line_; }

private:
    UniqueFd fd_;
    UioLine line_{};
};

}  // namespace mister::os
