// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "infra/json_name.h"

namespace mister::infra {

class JsonOut {
public:
    static constexpr std::size_t kCapacity = 4094;

    constexpr JsonOut() noexcept = default;

    [[nodiscard]] static constexpr JsonOut measuring() noexcept {
        JsonOut o;
        o.measure_ = true;
        return o;
    }

    constexpr void begin_record(JsonName t, std::uint32_t seq) noexcept {
        put_("{\"t\":\"");
        put_(t.view());
        put_("\",\"seq\":");
        number_(seq, std::numeric_limits<std::uint32_t>::max());
        comma_ = true;
    }
    constexpr void end_record() noexcept { put_('}'); }

    constexpr void begin_object(JsonName key) noexcept {
        key_(key);
        put_('{');
        comma_ = false;
    }
    constexpr void end_object() noexcept {
        put_('}');
        comma_ = true;
    }
    constexpr void begin_array(JsonName key) noexcept {
        key_(key);
        put_('[');
        comma_ = false;
    }
    constexpr void end_array() noexcept {
        put_(']');
        comma_ = true;
    }

    template <std::integral I>
    constexpr void field(JsonName key, I v) noexcept {
        key_(key);
        value_(v);
    }
    template <class E>
        requires std::is_enum_v<E>
    constexpr void field(JsonName key, E v) noexcept {
        key_(key);
        using U = std::underlying_type_t<E>;
        value_(static_cast<std::make_unsigned_t<U>>(static_cast<U>(v)));
    }
    template <std::integral I>
    constexpr void elem(I v) noexcept {
        comma_prefix_();
        value_(v);
    }

    constexpr void str(JsonName key, std::string_view v, std::size_t worst) noexcept {
        key_(key);
        put_('"');
        if (measure_) {
            need_ += worst;
        } else {
            put_(v);
        }
        put_('"');
        comma_ = true;
    }

    [[nodiscard]] constexpr std::size_t need() const noexcept { return need_; }
    [[nodiscard]] constexpr bool overflowed() const noexcept { return overflow_; }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(buf_.data(), len_);
    }

    [[nodiscard]] std::string_view take_line() noexcept {
        buf_[len_] = '\n';
        return std::string_view(buf_.data(), len_ + 1);
    }

private:
    constexpr void comma_prefix_() noexcept {
        if (comma_) put_(',');
    }
    constexpr void key_(JsonName key) noexcept {
        comma_prefix_();
        put_('"');
        put_(key.view());
        put_("\":");
    }

    constexpr void put_(char c) noexcept { put_(std::string_view(&c, 1)); }
    constexpr void put_(std::string_view s) noexcept {
        need_ += s.size();
        if (measure_ || overflow_) return;
        if (len_ + s.size() > kCapacity) {
            overflow_ = true;
            return;
        }
        for (const char c : s)
            buf_[len_++] = c;
    }

    template <std::integral I>
    constexpr void value_(I v) noexcept {
        if constexpr (std::is_same_v<I, bool>) {
            number_(v ? 1u : 0u, 1u);
        } else if constexpr (std::is_signed_v<I>) {
            const bool neg = v < 0;

            const auto mag = neg ? static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(v)
                                 : static_cast<std::uint64_t>(v);
            const auto worst = static_cast<std::uint64_t>(0) -
                               static_cast<std::uint64_t>(std::numeric_limits<I>::min());
            signed_number_(neg, mag, worst);
        } else {
            number_(static_cast<std::uint64_t>(v),
                    static_cast<std::uint64_t>(std::numeric_limits<I>::max()));
        }
        comma_ = true;
    }

    constexpr void signed_number_(bool neg, std::uint64_t mag, std::uint64_t worst_mag) noexcept {
        if (measure_) {
            need_ += 1 + digits_(worst_mag);
            return;
        }
        if (neg) put_('-');
        digits_out_(mag);
    }
    constexpr void number_(std::uint64_t v, std::uint64_t worst) noexcept {
        if (measure_) {
            need_ += digits_(worst);
            return;
        }
        digits_out_(v);
    }
    static constexpr std::size_t digits_(std::uint64_t v) noexcept {
        std::size_t n = 1;
        while (v >= 10) {
            v /= 10;
            ++n;
        }
        return n;
    }
    constexpr void digits_out_(std::uint64_t v) noexcept {
        std::array<char, 20> rev{};
        std::size_t n = 0;
        do {
            rev[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v != 0);
        std::array<char, 20> fwd{};
        for (std::size_t i = 0; i < n; ++i)
            fwd[i] = rev[n - 1 - i];
        put_(std::string_view(fwd.data(), n));
    }

    std::array<char, kCapacity + 1> buf_{};
    std::size_t len_ = 0;
    std::size_t need_ = 0;
    bool comma_ = false;
    bool measure_ = false;
    bool overflow_ = false;
};

template <class T>
[[nodiscard]] consteval std::size_t json_worst_len() {
    JsonOut o = JsonOut::measuring();
    to_json(o, T{});
    return o.need();
}

[[nodiscard]] consteval std::size_t json_record_worst(JsonName t, std::size_t members) {
    return 6 + t.view().size() + 8 + 10 + (members == 0 ? 0 : members + 1) + 1;
}

}  // namespace mister::infra
