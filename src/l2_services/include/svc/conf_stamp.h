// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>

#include <array>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::svc {

class WpaConfStore;

class ConfStamp {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static constexpr ConfStamp absent() noexcept { return ConfStamp{}; }

    [[nodiscard]] constexpr bool present() const noexcept { return present_; }

    friend constexpr bool operator==(const ConfStamp& a, const ConfStamp& b) noexcept {
        return a.present_ == b.present_
            && a.mtime_sec_ == b.mtime_sec_
            && a.mtime_nsec_ == b.mtime_nsec_
            && a.size_ == b.size_;
    }

private:
    friend class WpaConfStore;

    constexpr ConfStamp() noexcept = default;

    [[nodiscard]] static ConfStamp from_stat(const struct stat& st) noexcept {
        ConfStamp out;
        out.mtime_sec_ = std::int64_t{st.st_mtim.tv_sec};
        out.mtime_nsec_ = std::int64_t{st.st_mtim.tv_nsec};
        out.size_ = std::int64_t{st.st_size};
        out.present_ = true;
        return out;
    }

    std::int64_t mtime_sec_ = 0;
    std::int64_t mtime_nsec_ = 0;
    std::int64_t size_ = 0;
    bool present_ = false;
    std::array<std::uint8_t, 7> pad_{};
};

static_assert(std::is_trivially_copyable_v<ConfStamp>);
static_assert(std::has_unique_object_representations_v<ConfStamp>);
static_assert(sizeof(ConfStamp) == 32);

}
