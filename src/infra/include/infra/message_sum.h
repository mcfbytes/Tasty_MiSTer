// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace mister::infra {

template <class M>
concept MessageSum =
    std::is_trivially_copyable_v<M> && std::is_standard_layout_v<M> && requires(M m) {
        typename M::Kind;
        typename M::Alternatives;
        { M::kStore } -> std::convertible_to<std::size_t>;
        requires std::same_as<std::remove_cvref_t<decltype(m.kind)>, typename M::Kind>;
        requires std::same_as<std::remove_cvref_t<decltype(m.store)>,
                              std::array<std::byte, M::kStore>>;
    };

template <class M>
concept HasHead = MessageSum<M> && requires(M m) {
    typename M::Head;
    m.head;
};

template <class A, class Tuple>
inline constexpr bool in_tuple_v = false;
template <class A, class... Ts>
inline constexpr bool in_tuple_v<A, std::tuple<Ts...>> = (std::is_same_v<A, Ts> || ...);

template <class A, class M>
concept AlternativeOf =
    MessageSum<M> && std::is_trivially_copyable_v<A> && sizeof(A) <= M::kStore &&
    alignof(A) <= alignof(M) && in_tuple_v<A, typename M::Alternatives> &&
    std::same_as<std::remove_cvref_t<decltype(A::kKind)>, typename M::Kind>;

namespace detail {
template <class S>
consteval auto sink_result_() {
    if constexpr (requires { typename S::Result; })
        return std::type_identity<typename S::Result>{};
    else
        return std::type_identity<void>{};
}

template <class A>
consteval bool byte_deterministic_() {
    return std::is_empty_v<A> || std::has_unique_object_representations_v<A>;
}
}  // namespace detail

template <class S>
using SinkResult = typename decltype(detail::sink_result_<S>())::type;

template <MessageSum M>
consteval bool alternatives_are_total() {
    using Alts = typename M::Alternatives;
    constexpr std::size_t n = std::tuple_size_v<Alts>;
    if (n != static_cast<std::size_t>(M::Kind::kCount)) return false;
    if (!std::has_unique_object_representations_v<M>) return false;
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return ((std::tuple_element_t<I, Alts>::kKind == static_cast<typename M::Kind>(I) &&
                 AlternativeOf<std::tuple_element_t<I, Alts>, M> &&
                 detail::byte_deterministic_<std::tuple_element_t<I, Alts>>()) &&
                ...);
    }(std::make_index_sequence<n>{});
}

template <MessageSum M>
[[nodiscard]] constexpr typename M::Kind kind_of(const M& m) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    return m.kind;
}

template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr std::size_t ordinal(E e) noexcept {
    return static_cast<std::size_t>(e);
}

template <class Row, std::size_t N>
[[nodiscard]] consteval bool rows_are_ordinal(const std::array<Row, N>& rows) {
    using K = std::remove_cvref_t<decltype(rows[0].kind)>;
    if (N != ordinal(K::kCount)) return false;
    for (std::size_t i = 0; i < N; ++i)
        if (ordinal(rows[i].kind) != i) return false;
    return true;
}

namespace detail {

template <class A, MessageSum M>
    requires AlternativeOf<A, M>
[[nodiscard]] A get_(const M& m) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    A a{};
    if constexpr (!std::is_empty_v<A>) std::memcpy(&a, m.store.data(), sizeof(A));
    return a;
}
}  // namespace detail

template <MessageSum M, AlternativeOf<M> A>
    requires(!HasHead<M>)
[[nodiscard]] M make(const A& a) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    M m{};
    m.kind = A::kKind;
    if constexpr (!std::is_empty_v<A>) std::memcpy(m.store.data(), &a, sizeof(A));
    return m;
}

template <HasHead M, AlternativeOf<M> A>
[[nodiscard]] M make(const A& a, const typename M::Head& h) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    M m{};
    m.kind = A::kKind;
    m.head = h;
    if constexpr (!std::is_empty_v<A>) std::memcpy(m.store.data(), &a, sizeof(A));
    return m;
}

template <class A, MessageSum M>
    requires AlternativeOf<A, M>
[[nodiscard]] std::optional<A> as(const M& m) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    if (m.kind != A::kKind) return std::nullopt;
    return detail::get_<A>(m);
}

struct AllRouted {
    template <class A>
    static constexpr bool routed = true;
};

namespace detail {

template <class A, class M, class S, class... C>
consteval bool accepts_() {
    if constexpr (HasHead<M>)
        return requires(S& s, const M& m, C&&... c) {
            s.on(std::declval<A>(), m.head, std::forward<C>(c)...);
        } || requires(S& s, const M& m, C&&... c) {
            s.on(std::declval<A&>(), m.head, std::forward<C>(c)...);
        };
    else
        return requires(S& s, C&&... c) { s.on(std::declval<A>(), std::forward<C>(c)...); } ||
               requires(S& s, C&&... c) { s.on(std::declval<A&>(), std::forward<C>(c)...); };
}

template <class Routes, class A, MessageSum M, class S, class... C>
SinkResult<S> arm_(const M& m, S& s, C&&... c) noexcept {
    if constexpr (Routes::template routed<A>) {
        if constexpr (HasHead<M>)
            return s.on(get_<A>(m), m.head, std::forward<C>(c)...);
        else
            return s.on(get_<A>(m), std::forward<C>(c)...);
    } else {
        static_assert(!accepts_<A, M, S, C...>(),
                      "an on() overload for an alternative this sink is not routed");
        return s.misrouted(m, std::forward<C>(c)...);
    }
}
}  // namespace detail

template <class Routes, MessageSum M, class S, class... C>
SinkResult<S> dispatch(const M& m, S& s, C&&... c) noexcept {
    static_assert(alternatives_are_total<M>(), "a message sum whose alternatives are not total");
    using R = SinkResult<S>;
    using Alts = typename M::Alternatives;
    using K = typename M::Kind;
    return [&]<std::size_t... I> [[gnu::always_inline]] (std::index_sequence<I...>) -> R {
        if constexpr (std::is_void_v<R>) {
            const bool hit = ((kind_of(m) == static_cast<K>(I)
                                   ? (detail::arm_<Routes, std::tuple_element_t<I, Alts>>(
                                          m, s, std::forward<C>(c)...),
                                      true)
                                   : false) ||
                              ...);
            if (!hit) s.misrouted(m, std::forward<C>(c)...);
        } else {
            R r{};
            const bool hit = ((kind_of(m) == static_cast<K>(I)
                                   ? (r = detail::arm_<Routes, std::tuple_element_t<I, Alts>>(
                                          m, s, std::forward<C>(c)...),
                                      true)
                                   : false) ||
                              ...);
            if (!hit) r = s.misrouted(m, std::forward<C>(c)...);
            return r;
        }
    }(std::make_index_sequence<std::tuple_size_v<Alts>>{});
}

}  // namespace mister::infra
