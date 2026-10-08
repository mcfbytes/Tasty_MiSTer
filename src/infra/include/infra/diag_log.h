// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "infra/json_out.h"
#include "infra/unique_fd.h"
#include "infra/seat.h"

namespace mister::xthread {

class DiagLog {
    TASTY_SEAT_MEDIATOR(Any, Any);

public:
    static constexpr std::size_t kMaxLine = 4095;
    static_assert(kMaxLine + 1 <= PIPE_BUF, "one record must still be one atomic write");
    static_assert(infra::JsonOut::kCapacity == kMaxLine - 1,
                  "a JsonOut record is exactly what appendf keeps");

    DiagLog() = default;

    bool open(const char* path) noexcept {
        fd_ = UniqueFd(::open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644));
        return fd_.valid();
    }
    bool is_open() const noexcept { return fd_.valid(); }

    void set_echo_stderr(bool on) noexcept { echo_stderr_ = on; }

    [[gnu::format(printf, 2, 3)]]
    void appendf(const char* fmt, ...) noexcept {
        char buf[kMaxLine + 1];
        va_list ap;
        va_start(ap, fmt);
        int n = std::vsnprintf(buf, sizeof buf - 1, fmt, ap);
        va_end(ap);
        if (n < 0) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto len = static_cast<std::size_t>(n);
        if (len > sizeof buf - 2) {
            truncations_.fetch_add(1, std::memory_order_relaxed);
            n = std::snprintf(buf, sizeof buf - 1, "{\"t\":\"trunc\",\"need\":%u,\"max\":%u}",
                              static_cast<unsigned>(len), static_cast<unsigned>(sizeof buf - 2));
            len = (n < 0) ? 0u : static_cast<std::size_t>(n);
            if (len > sizeof buf - 2) len = sizeof buf - 2;
        }
        buf[len] = '\n';
        emit_(buf, len + 1);
    }

    void append(infra::JsonOut& o) noexcept {
        if (!o.overflowed()) {
            const std::string_view line = o.take_line();
            emit_(line.data(), line.size());
            return;
        }
        truncations_.fetch_add(1, std::memory_order_relaxed);
        char buf[96];
        const int n = std::snprintf(buf, sizeof buf - 1, "{\"t\":\"trunc\",\"need\":%u,\"max\":%u}",
                                    static_cast<unsigned>(o.need()),
                                    static_cast<unsigned>(infra::JsonOut::kCapacity));
        const auto len = n < 0 ? 0u : static_cast<std::size_t>(n);
        buf[len] = '\n';
        emit_(buf, len + 1);
    }

    std::uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

    std::uint32_t truncations() const noexcept {
        return truncations_.load(std::memory_order_relaxed);
    }

private:
    void emit_(const char* line, std::size_t len) noexcept {
        if (echo_stderr_) {
            const auto e = ::write(STDERR_FILENO, line, len);
            (void)e;
        }
        if (!fd_.valid()) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (::write(fd_.get(), line, len) != static_cast<::ssize_t>(len)) {

            drops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    UniqueFd fd_;

    std::atomic<std::uint32_t> drops_{0};
    std::atomic<std::uint32_t> truncations_{0};
    bool echo_stderr_ = false;
};

}  // namespace mister::xthread
