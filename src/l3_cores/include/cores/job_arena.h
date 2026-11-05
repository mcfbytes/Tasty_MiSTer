// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <new>

namespace mister::cores {

class JobArena {
public:
    template <class Job, class... Args>
    [[nodiscard]] Job* emplace(Args&&... args) noexcept {
        static_assert(sizeof(Job) <= kBytes && alignof(Job) <= kAlign);
        return ::new (static_cast<void*>(bytes_)) Job(static_cast<Args&&>(args)...);
    }

private:
    enum : std::size_t { kBytes = 640u * 1024u };

    enum : std::size_t { kAlign = 64 };

    alignas(kAlign) std::byte bytes_[kBytes];
};

}
