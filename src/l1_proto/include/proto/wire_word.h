// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "hal/types.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::proto {

template <class F>
[[nodiscard]] Ex<std::size_t> for_each_word(
    hal::Width w,
    std::span<const std::uint8_t> bytes,
    F&& f
) {
    std::size_t at = 0;
    if (w == hal::Width::Word) {
        for (; at + 1u < bytes.size(); at += 2u) {
            const auto pair = infra::load_le<std::uint16_t>(bytes.subspan(at).first<2>());
            auto r = f(hal::LinkWord{pair});
            if (!r)
                return std::unexpected(r.error());
            if (!*r)
                return at + 2u;
        }
        if (at < bytes.size()) {
            auto r = f(hal::LinkWord{bytes.back()});
            if (!r)
                return std::unexpected(r.error());
        }
        return bytes.size();
    }
    for (; at < bytes.size(); ++at) {
        auto r = f(hal::LinkWord{bytes[at]});
        if (!r)
            return std::unexpected(r.error());
        if (!*r)
            return at + 1u;
    }
    return at;
}

template <class F>
[[nodiscard]] Ex<void> fill_from_words(hal::Width w, std::span<std::uint8_t> out, F&& next) {
    std::size_t at = 0;
    if (w == hal::Width::Word) {
        for (; at + 1u < out.size(); at += 2u) {
            auto r = next();
            if (!r)
                return std::unexpected(r.error());
            r->store_le(out.subspan(at).first<2>());
        }
        if (at < out.size()) {
            auto r = next();
            if (!r)
                return std::unexpected(r.error());
            out.back() = r->low_byte();
        }
        return {};
    }
    for (std::uint8_t& byte : out) {
        auto r = next();
        if (!r)
            return std::unexpected(r.error());
        byte = r->low_byte();
    }
    return {};
}

}
