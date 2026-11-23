// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "infra/affine.h"
#include "infra/seat.h"

namespace mister::infra {

template <class Index>
concept SetIndex = VShape<Index> || std::is_enum_v<Index>;

void index_outside_set_in_constant_expression();

template <class Index, std::size_t N, class Rep = std::uint64_t>
class IndexSet {
    TASTY_SEAT_EXEMPT(component);
    static_assert(SetIndex<Index>, "a set index is a {v} value or an enum");
    static_assert(N > 0, "a set no index can reach");
    static_assert(std::unsigned_integral<Rep>, "a set's rep is an unsigned word");

public:
    using index_type = Index;
    using word_type = Rep;
    static constexpr std::size_t kSize = N;
    static constexpr std::size_t kBitsPerWord = std::numeric_limits<Rep>::digits;
    static constexpr std::size_t kWordCount = (N + kBitsPerWord - 1) / kBitsPerWord;

    using rep_type = std::conditional_t<kWordCount == 1, Rep, std::array<Rep, kWordCount>>;

    rep_type v{};

    [[nodiscard]] static constexpr bool addresses(Index index) noexcept {
        return ordinal_of(index) < N;
    }

    [[nodiscard]] constexpr bool contains(Index index) const noexcept {
        if (!addresses(index))
            return false;
        const std::size_t pos = ordinal_of(index);
        return (word_at(word_of(pos)) & bit_of(pos)) != Rep{0};
    }

    [[nodiscard]] constexpr bool contains(IndexSet other) const noexcept {
        for (std::size_t w = 0; w < kWordCount; ++w)
            if ((word_at(w) & other.word_at(w)) != other.word_at(w))
                return false;
        return true;
    }

    [[nodiscard]] constexpr bool intersects(IndexSet other) const noexcept {
        for (std::size_t w = 0; w < kWordCount; ++w)
            if ((word_at(w) & other.word_at(w)) != Rep{0})
                return true;
        return false;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        for (std::size_t w = 0; w < kWordCount; ++w)
            if (word_at(w) != Rep{0})
                return false;
        return true;
    }

    [[nodiscard]] constexpr IndexSet with(Index index) const noexcept {
        IndexSet out = *this;
        out.insert(ordinal_of(index));
        return out;
    }

    [[nodiscard]] constexpr IndexSet with(IndexSet other) const noexcept {
        IndexSet out{};
        for (std::size_t w = 0; w < kWordCount; ++w)
            out.word_set(w, static_cast<Rep>(word_at(w) | other.word_at(w)));
        return out;
    }

    [[nodiscard]] constexpr IndexSet without(Index index) const noexcept {
        IndexSet out = *this;
        out.erase(ordinal_of(index));
        return out;
    }

    [[nodiscard]] constexpr IndexSet without(IndexSet drop) const noexcept {
        IndexSet out{};
        for (std::size_t w = 0; w < kWordCount; ++w)
            out.word_set(w, static_cast<Rep>(word_at(w) & ~drop.word_at(w)));
        return out;
    }

    [[nodiscard]] constexpr IndexSet only(IndexSet other) const noexcept {
        IndexSet out{};
        for (std::size_t w = 0; w < kWordCount; ++w)
            out.word_set(w, static_cast<Rep>(word_at(w) & other.word_at(w)));
        return out;
    }

    [[nodiscard]] constexpr IndexSet differing(IndexSet other) const noexcept {
        IndexSet out{};
        for (std::size_t w = 0; w < kWordCount; ++w)
            out.word_set(w, static_cast<Rep>(word_at(w) ^ other.word_at(w)));
        return out;
    }

    [[nodiscard]] constexpr IndexSet with_or_unchanged(Index index) const noexcept {
        return addresses(index) ? with(index) : *this;
    }

    [[nodiscard]] constexpr IndexSet without_or_unchanged(Index index) const noexcept {
        return addresses(index) ? without(index) : *this;
    }

    [[nodiscard]] constexpr IndexSet with_or_all(Index index) const noexcept {
        return addresses(index) ? with(index) : all();
    }

    [[nodiscard]] static constexpr IndexSet all() noexcept {
        IndexSet out{};
        for (std::size_t w = 0; w < kWordCount; ++w)
            out.word_set(w, filled_word());
        out.word_set(kWordCount - 1, static_cast<Rep>(out.word_at(kWordCount - 1) & tail_mask()));
        return out;
    }

    [[nodiscard]] constexpr std::optional<Index> lowest() const noexcept {
        for (std::size_t w = 0; w < kWordCount; ++w) {
            const Rep word = masked_word(w);
            if (word == Rep{0})
                continue;
            return index_of(w * kBitsPerWord + static_cast<std::size_t>(std::countr_zero(word)));
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<Index> highest() const noexcept {
        for (std::size_t w = kWordCount; w-- > 0;) {
            const Rep word = masked_word(w);
            if (word == Rep{0})
                continue;
            const std::size_t top = static_cast<std::size_t>(std::countl_zero(word));
            return index_of(w * kBitsPerWord + (kBitsPerWord - 1 - top));
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<Index> pop_lowest() noexcept {
        const std::optional<Index> index = lowest();
        if (index)
            erase(ordinal_of(*index));
        return index;
    }

    template <class Each>
    constexpr void drain(Each&& each) {
        for (std::size_t w = 0; w < kWordCount; ++w) {
            for (Rep word = masked_word(w); word != Rep{0}; word = masked_word(w)) {
                const std::size_t pos = w * kBitsPerWord
                    + static_cast<std::size_t>(std::countr_zero(word));
                erase(pos);
                each(index_of(pos));
            }
        }
    }

    friend constexpr bool operator==(const IndexSet&, const IndexSet&) = default;

private:
    [[nodiscard]] static constexpr std::size_t ordinal_of(Index index) noexcept {
        if constexpr (std::is_enum_v<Index>) {
            return std::to_underlying(index);
        } else {
            return index.v;
        }
    }

    [[nodiscard]] static constexpr Index index_of(std::size_t pos) noexcept {
        if constexpr (std::is_enum_v<Index>) {
            return static_cast<Index>(pos);
        } else {
            using RepOf = std::remove_cvref_t<decltype(std::declval<Index&>().v)>;
            return Index{static_cast<RepOf>(pos)};
        }
    }

    [[nodiscard]] static constexpr std::size_t word_of(std::size_t pos) noexcept {
        return pos / kBitsPerWord;
    }

    [[nodiscard]] static constexpr Rep bit_of(std::size_t pos) noexcept {
        return static_cast<Rep>(Rep{1} << (pos % kBitsPerWord));
    }

    [[nodiscard]] static constexpr Rep filled_word() noexcept { return static_cast<Rep>(~Rep{0}); }

    [[nodiscard]] static constexpr Rep tail_mask() noexcept {
        constexpr std::size_t kTail = N % kBitsPerWord;
        if constexpr (kTail == 0)
            return filled_word();
        return static_cast<Rep>(static_cast<Rep>(Rep{1} << kTail) - Rep{1});
    }

    [[nodiscard]] constexpr Rep word_at(std::size_t w) const noexcept {
        if constexpr (kWordCount == 1) {
            (void)w;
            return v;
        } else {
            return v[w];
        }
    }

    constexpr void word_set(std::size_t w, Rep word) noexcept {
        if constexpr (kWordCount == 1) {
            (void)w;
            v = word;
        } else {
            v[w] = word;
        }
    }

    [[nodiscard]] constexpr Rep masked_word(std::size_t w) const noexcept {
        return w + 1 == kWordCount ? static_cast<Rep>(word_at(w) & tail_mask()) : word_at(w);
    }

    [[nodiscard]] static constexpr bool reaches(std::size_t pos) noexcept {
        if (pos < N)
            return true;
        if consteval {
            index_outside_set_in_constant_expression();
        }
        return false;
    }

    constexpr void insert(std::size_t pos) noexcept {
        if (!reaches(pos))
            return;
        const std::size_t w = word_of(pos);
        word_set(w, static_cast<Rep>(word_at(w) | bit_of(pos)));
    }

    constexpr void erase(std::size_t pos) noexcept {
        if (!reaches(pos))
            return;
        const std::size_t w = word_of(pos);
        word_set(w, static_cast<Rep>(word_at(w) & ~bit_of(pos)));
    }
};

}
