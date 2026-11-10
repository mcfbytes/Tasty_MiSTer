// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "infra/affine.h"
#include "infra/json_name.h"
#include "infra/json_width.h"

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

    constexpr void begin_record(JsonName t) noexcept {
        put_("{\"t\":\"");
        put_(t.view());
        put_('"');
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
        requires(
            std::is_enum_v<E> && !requires(JsonOut& o, JsonName k, E e) { json_field(o, k, e); }
        )
    constexpr void field(JsonName key, E v) noexcept {
        key_(key);
        using U = std::underlying_type_t<E>;
        value_(static_cast<std::make_unsigned_t<U>>(static_cast<U>(v)));
    }

    template <class R>
        requires(affine::Affine<R> && std::integral<decltype(R::v)>)
    constexpr void field(JsonName key, R r) noexcept {
        const auto [number] = r;
        field(key, number);
    }

    template <class T>
        requires requires(JsonOut& o, JsonName k, const T& t) { json_field(o, k, t); }
    constexpr void field(JsonName key, const T& t) noexcept {
        json_field(*this, key, t);
    }

    template <std::integral I>
    constexpr void elem(I v) noexcept {
        comma_prefix_();
        value_(v);
    }

    template <class T>
        requires requires(JsonOut& o, const T& t) { json_elem(o, t); }
    constexpr void elem(const T& t) noexcept {
        json_elem(*this, t);
    }

    constexpr void str(JsonName key, std::string_view v, JsonWidth worst) noexcept {
        key_(key);
        put_('"');
        if (measure_) {
            need_ += worst.bytes_;
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
        if (comma_)
            put_(',');
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
        if (measure_ || overflow_)
            return;
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
            const auto worst = static_cast<std::uint64_t>(0)
                - static_cast<std::uint64_t>(std::numeric_limits<I>::min());
            signed_number_(neg, mag, worst);
        } else {
            number_(
                static_cast<std::uint64_t>(v),
                static_cast<std::uint64_t>(std::numeric_limits<I>::max())
            );
        }
        comma_ = true;
    }

    constexpr void signed_number_(bool neg, std::uint64_t mag, std::uint64_t worst_mag) noexcept {
        if (measure_) {
            need_ += 1 + digits_(worst_mag);
            return;
        }
        if (neg)
            put_('-');
        digits_out_(mag);
    }

    constexpr void number_(std::uint64_t v, std::uint64_t worst) noexcept {
        if (measure_) {
            need_ += digits_(worst);
            return;
        }
        digits_out_(v);
    }

    enum : std::uint64_t { kRadix = 10 };

    static constexpr std::size_t kMaxDigits = std::numeric_limits<std::uint64_t>::digits10 + 1;

    static constexpr std::size_t digits_(std::uint64_t v) noexcept {
        std::size_t n = 1;
        while (v >= kRadix) {
            v /= kRadix;
            ++n;
        }
        return n;
    }

    constexpr void digits_out_(std::uint64_t v) noexcept {

        std::array<char, kMaxDigits> buf{};
        std::size_t at = buf.size();
        do {
            buf[--at] = static_cast<char>('0' + v % kRadix);
            v /= kRadix;
        } while (v != 0);
        put_(std::string_view(buf.data() + at, buf.size() - at));
    }

    std::array<char, kCapacity + 1> buf_{};
    std::size_t len_ = 0;
    std::size_t need_ = 0;
    bool comma_ = false;
    bool measure_ = false;
    bool overflow_ = false;
};

template <std::size_t N>
[[nodiscard]] consteval JsonWidth json_longest(const std::array<const char*, N>& names) noexcept {
    return JsonWidth::longest(names);
}

template <class T>
[[nodiscard]] consteval std::size_t json_worst_len() {
    JsonOut o = JsonOut::measuring();
    to_json(o, T{});
    return o.need();
}

[[nodiscard]] consteval std::size_t json_record_worst(JsonName t, std::size_t members) {
    enum : std::size_t {
        kOpenAndTypeKey = 6,
        kSeqKey = 8,
        kSeqDigits = 10,
    };

    return kOpenAndTypeKey
        + t.view().size()
        + kSeqKey
        + kSeqDigits
        + (members == 0 ? 0 : members + 1)
        + 1;
}

}
