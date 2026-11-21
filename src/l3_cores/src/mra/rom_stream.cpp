// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra/rom_stream.h"

#include <algorithm>
#include <utility>

#include "cores/mra/rom_set_assembler.h"
#include "infra/byte_view.h"
#include "infra/span_cut.h"

namespace mister::cores::mra {

namespace {

struct Overlap {
    RomOffset lo{};
    RomOffset hi{};

    [[nodiscard]] static Overlap of(
        RomOffset at,
        RomLength len,
        RomOffset from,
        std::span<const std::uint8_t> out
    ) noexcept {
        const RomOffset lo = std::max(at, from);
        const RomOffset hi = std::min(at + len, from + RomLength::of(out));
        return Overlap{lo, std::max(lo, hi)};
    }

    [[nodiscard]] bool empty() const noexcept { return hi == lo; }

    template <class T>
    [[nodiscard]] std::span<T> in(std::span<T> s, RomOffset origin) const noexcept {
        return infra::affine::first_clamped(
            infra::affine::from_clamped(s, RomOffset{} + (lo - origin)),
            hi - lo
        );
    }
};

}

RomStream::RomStream(const svc::Vfs& vfs, RomPlan plan) : vfs_(vfs) {
    end_ = RomOffset{} + plan.length;
    items_.reserve(plan.items.size());
    for (RomPlan::Item& item : plan.items) {
        items_.push_back(Live{.item = std::move(item), .cur = {}});
    }
}

Ex<void> RomStream::read(RomOffset from, std::span<std::uint8_t> out) {
    TASTY_SEAT_BODY(RomStream);
    std::ranges::fill(out, std::uint8_t{0});
    if (from < next_) {
        for (Live& l : items_) {
            l.cur = Cursor{};
        }
    }
    for (Live& l : items_) {
        if (const auto* s = std::get_if<RomPlan::Segment>(&l.item)) {
            if (auto r = lay_(*s, l.cur, from, out); !r)
                return r;
        } else if (const auto* r = std::get_if<FillRun>(&l.item)) {
            lay_(*r, from, out);
        } else if (const auto* p = std::get_if<RomPlan::Patch>(&l.item)) {
            lay_(*p, from, out);
        }
    }
    next_ = from + RomLength::of(out);
    return {};
}

Ex<void> RomStream::lay_(
    const RomPlan::Segment& s,
    Cursor& c,
    RomOffset from,
    std::span<std::uint8_t> piece
) {
    const infra::IndexedSpan<std::uint8_t, RomOffset> out{piece};

    const std::optional<MapWalk> walk = s.map.walk(s.unit);
    if (!walk)
        return {};
    const LaneWrites& writes = walk->writes;
    const RomOffset to = from + out.size();
    if (!c.started) {
        c.pos = s.base;
        c.started = true;
    }
    RomLength at = c.at;
    RomOffset pos = c.pos;
    bool settled = true;
    while (at < s.len && pos < to) {
        const RomLength len = (s.len - at).min(s.chunk);
        const RomOffset reach = pos + writes.reach(len, s.unit);
        if (reach > from) {
            auto src = source_(s, c, at);
            if (!src)
                return std::unexpected(src.error());

            RomOffset unit = pos;
            std::span<const std::uint8_t> rest = *src;
            const RomOffset lead = pos + writes.highest();
            if (from > lead) {
                const UnitCount skip = (from - lead).units_within(s.unit.bytes());
                unit += skip.times(s.unit.bytes());
                rest = infra::affine::from_clamped(
                    rest,
                    RomOffset{} + skip.times(writes.per_unit())
                );
            }
            for (; !rest.empty() && unit < to;) {
                const std::span<const std::uint8_t> one = infra::affine::first_clamped(
                    rest,
                    writes.per_unit()
                );
                for (std::size_t i = 0; i < one.size(); ++i) {
                    const RomOffset p = unit + writes.offset(i);
                    if (p >= from && p < to)
                        out[RomOffset{} + (p - from)] = one[i];
                }
                rest = rest.subspan(one.size());
                unit += s.unit.bytes();
            }
        }
        const RomOffset next = pos + len.units_covering(writes.per_unit()).times(s.unit.bytes());
        at += len;
        pos = next;
        settled = settled && reach <= to;
        if (settled) {
            c.at = at;
            c.pos = pos;
        }
    }
    if (c.at >= s.len) {
        c.file.reset();
        c.chunk = {};
        c.loaded = false;
    }
    return {};
}

Ex<std::span<const std::uint8_t>> RomStream::source_(
    const RomPlan::Segment& s,
    Cursor& c,
    RomLength at
) {
    const RomLength len = (s.len - at).min(s.chunk);
    if (s.text)
        return infra::affine::first_clamped(std::span<const std::uint8_t>(*s.text), len);
    if (c.loaded && c.held == at) {
        return infra::affine::first_clamped(std::span<const std::uint8_t>(c.chunk), len);
    }
    if (!c.file) {
        auto f = vfs_.open_zip_by_crc(s.path, s.crc);
        if (!f)
            return std::unexpected(f.error());
        c.file = std::move(*f);
    }
    c.chunk.assign(kStreamChunkBytes, std::uint8_t{0});
    const std::span<std::uint8_t> buf = infra::affine::first_clamped(
        std::span<std::uint8_t>(c.chunk),
        len
    );
    (void)c.file->read_at(s.from + at.file_size(), std::as_writable_bytes(buf));
    c.held = at;
    c.loaded = true;
    return std::span<const std::uint8_t>(buf);
}

void RomStream::lay_(const FillRun& r, RomOffset from, std::span<std::uint8_t> out) noexcept {
    const Overlap o = Overlap::of(r.at, r.len, from, out);
    if (o.empty())
        return;
    r.write_to(o.in(out, from), o.lo - r.at);
}

void RomStream::lay_(
    const RomPlan::Patch& p,
    RomOffset from,
    std::span<std::uint8_t> out
) noexcept {
    const Overlap o = Overlap::of(p.at, RomLength::of(p.bytes), from, out);
    if (o.empty())
        return;
    const std::span<std::uint8_t> dst = o.in(out, from);
    const std::span<const std::uint8_t> src = o.in(std::span<const std::uint8_t>(p.bytes), p.at);
    if (!p.xor_op) {
        std::ranges::copy(src, dst.begin());
        return;
    }
    for (std::size_t i = 0; i < dst.size(); ++i) {
        dst[i] ^= src[i];
    }
}

}
