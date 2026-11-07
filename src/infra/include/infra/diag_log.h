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
    enum : unsigned { kLogFileMode = 0644, kTerminatorBytes = 2 };

    static constexpr std::size_t kMaxLine = 4095;
    static constexpr std::size_t kRecordMax = kMaxLine - 1;

    static constexpr std::size_t kTruncRecordMax = 96;
    static_assert(kMaxLine + 1 <= PIPE_BUF, "one record must still be one atomic write");
    static_assert(
        infra::JsonOut::kCapacity == kMaxLine - 1,
        "a JsonOut record is exactly what appendf keeps"
    );

    DiagLog() = default;

    bool open(const char* path) noexcept {
        fd_ = UniqueFd(::open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, kLogFileMode));
        return fd_.valid();
    }

    bool is_open() const noexcept { return fd_.valid(); }

    void set_echo_stderr(bool on) noexcept { echo_stderr_ = on; }

    [[gnu::format(printf, 2, 3)]]
    void appendf(const char* fmt, ...) noexcept {
        char buf[kMaxLine + 1];
        va_list ap;
        va_start(ap, fmt);
        const int n = std::vsnprintf(buf, sizeof buf - 1, fmt, ap);
        va_end(ap);
        if (n < 0) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto len = static_cast<std::size_t>(n);
        if (len > kRecordMax) {
            truncations_.fetch_add(1, std::memory_order_relaxed);
            trunc_record_(len);
            return;
        }
        buf[len] = '\n';
        emit_(std::string_view(buf, len + 1));
    }

    void append(infra::JsonOut& o) noexcept {
        if (!o.overflowed()) {
            const std::string_view line = o.take_line();
            emit_(line);
            return;
        }
        truncations_.fetch_add(1, std::memory_order_relaxed);
        trunc_record_(o.need());
    }

    std::uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

    std::uint32_t truncations() const noexcept {
        return truncations_.load(std::memory_order_relaxed);
    }

private:
    void trunc_record_(std::size_t need) noexcept {
        char buf[kTruncRecordMax];
        const int n = std::snprintf(
            buf,
            sizeof buf - 1,
            kTruncFormat,
            static_cast<unsigned>(need),
            static_cast<unsigned>(kRecordMax)
        );
        std::size_t len = n < 0 ? 0u : static_cast<std::size_t>(n);
        if (len > sizeof buf - kTerminatorBytes)
            len = sizeof buf - kTerminatorBytes;
        buf[len] = '\n';
        emit_(std::string_view(buf, len + 1));
    }

    void emit_(std::string_view line) noexcept {
        if (echo_stderr_) {
            const auto e = ::write(STDERR_FILENO, line.data(), line.size());
            (void)e;
        }
        if (!fd_.valid()) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (::write(fd_.get(), line.data(), line.size()) != static_cast<::ssize_t>(line.size())) {

            drops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static constexpr const char* kTruncFormat = "{\"t\":\"trunc\",\"need\":%u,\"max\":%u}";

    UniqueFd fd_;

    std::atomic<std::uint32_t> drops_{0};
    std::atomic<std::uint32_t> truncations_{0};
    bool echo_stderr_ = false;
};

}
