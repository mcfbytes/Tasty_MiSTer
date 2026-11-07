// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "cores/types.h"
#include "infra/byte_view.h"
#include "infra/indexed_span.h"
#include "infra/seat.h"
#include "os/anon_mapping.h"

namespace mister::cores::mra {

class RomBytes {
    TASTY_SEAT_EXEMPT(component);

public:
    RomBytes() noexcept = default;
    ~RomBytes() = default;

    RomBytes(RomBytes&& o) noexcept : map_(std::move(o.map_)), end_(std::exchange(o.end_, {})) {}

    RomBytes& operator=(RomBytes&& o) noexcept {
        map_ = std::move(o.map_);
        end_ = std::exchange(o.end_, {});
        return *this;
    }

    RomBytes(const RomBytes&) = delete;
    RomBytes& operator=(const RomBytes&) = delete;

    [[nodiscard]] bool resize(RomOffset end) noexcept;

    void clear() noexcept { end_ = RomOffset{}; }

    [[nodiscard]] RomOffset extent() const noexcept { return end_; }

    [[nodiscard]] std::size_t size() const noexcept { return view().size(); }

    [[nodiscard]] bool empty() const noexcept { return end_ == RomOffset{}; }

    [[nodiscard]] std::size_t capacity() const noexcept { return map_.size(); }

    [[nodiscard]] std::uint8_t* data() noexcept { return mapped_().data(); }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return mapped_().data(); }

    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept {
        return end_.before(mapped_());
    }

    [[nodiscard]] const std::uint8_t* begin() const noexcept { return view().data(); }

    [[nodiscard]] const std::uint8_t* end() const noexcept { return view().data() + view().size(); }

    [[nodiscard]] std::uint8_t front() const noexcept { return view().front(); }

    [[nodiscard]] std::uint8_t back() const noexcept { return view().back(); }

    std::uint8_t& operator[](RomOffset i) noexcept { return cells_()[i]; }

    const std::uint8_t& operator[](RomOffset i) const noexcept { return cells_()[i]; }

    [[nodiscard]] std::span<std::uint8_t> slice(RomOffset at, RomLength n) noexcept {
        return n.first_of(at.onward(view_()));
    }

    [[nodiscard]] std::span<const std::uint8_t> from(RomOffset at) const noexcept {
        return at.onward(view());
    }

    void write(RomOffset at, std::span<const std::uint8_t> src) noexcept {
        const std::span<std::uint8_t> dst = slice(at, RomLength::of(src));
        assert(dst.size() == src.size());
        std::ranges::copy(src, dst.begin());
    }

    [[nodiscard]] bool operator==(std::span<const std::uint8_t> other) const noexcept {
        return std::ranges::equal(view(), other);
    }

private:
    [[nodiscard]] std::span<std::uint8_t> mapped_() noexcept {
        return infra::byte_cast<std::uint8_t>(map_.view());
    }

    [[nodiscard]] std::span<const std::uint8_t> mapped_() const noexcept {
        return infra::byte_cast<const std::uint8_t>(map_.view());
    }

    [[nodiscard]] std::span<std::uint8_t> view_() noexcept { return end_.before(mapped_()); }

    [[nodiscard]] infra::IndexedSpan<std::uint8_t, RomOffset> cells_() noexcept {
        return infra::IndexedSpan<std::uint8_t, RomOffset>{view_()};
    }

    [[nodiscard]] infra::IndexedSpan<const std::uint8_t, RomOffset> cells_() const noexcept {
        return infra::IndexedSpan<const std::uint8_t, RomOffset>{view()};
    }

    os::AnonMapping map_;
    RomOffset end_{};
};

}
