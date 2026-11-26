// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_osd_endpoint.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "infra/endian.h"
#include "infra/prelude.h"
#include "infra/seat_owner_ui.h"
#include "proto/types.h"

namespace mister::app {

namespace {

using ImageRow = std::array<hal::OsdBlock::Reg, hal::OsdBlock::kRowWords>;
constexpr proto::OsdRowCells<ImageRow, hal::OsdBlock::kImageRows> kImageRegs = [] {
    proto::OsdRowCells<ImageRow, hal::OsdBlock::kImageRows> regs{};
    for (std::size_t row = 0; row < regs.size(); ++row) {
        for (std::uint32_t w = 0; w < hal::OsdBlock::kRowWords; ++w)
            regs[proto::OsdRow::saturating(row)][w] = hal::OsdBlock::image(row, w);
    }
    return regs;
}();

}

bool LwOsdEndpoint::accepts_hook_(const OsdGrant& g) const noexcept {
    return g.block.lent();
}

void LwOsdEndpoint::publish_hook_(const OsdRoute& r) noexcept {
    w_.route.publish(r);
}

void LwOsdEndpoint::prepare_hook_(const OsdGrant& g) noexcept {
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
}

bool LwOsdEndpoint::arm_hook_(const OsdGrant&) noexcept {
    return true;
}

void LwOsdEndpoint::bind_hook_(const OsdGrant& g) noexcept {
    image_rows_ = g.body.rows < kRows ? g.body.rows : kRows;
    if (store_image_(bound_rows_()))
        ++tally_.frames;
}

void LwOsdEndpoint::bound_hook_() noexcept {
    publish_();
}

void LwOsdEndpoint::answer_hook_(proto::BuildGeneration) noexcept {}

void LwOsdEndpoint::unbind_hook_() noexcept {
    block_.reset();
    stored_ = {};
    image_rows_ = kRows;
}

void LwOsdEndpoint::serve_bound_() noexcept {
    store_frame_();
}

bool LwOsdEndpoint::idle_bound_hook_() const noexcept {
    return true;
}

void LwOsdEndpoint::store_frame_() noexcept {
    const proto::OsdRowCount n = bound_rows_();
    bool dirty = false;
    for (proto::OsdRow r{}; n.covers(r) && !dirty; r += proto::kOneOsdRow)
        dirty = w_.surface.seq(r) != stored_[r];
    if (!dirty)
        return;
    if (!lease_())
        return;
    if (store_dirty_(n))
        ++tally_.frames;
    release_();
    publish_();
}

bool LwOsdEndpoint::store_image_(proto::OsdRowCount n) noexcept {
    const hal::OsdStatus before = status_();
    Frame frame{};
    proto::OsdRowCount count{};
    for (proto::OsdRow r{}; n.covers(r); r += proto::kOneOsdRow) {
        if (!store_row_(r))
            continue;
        frame[count.end()] = r;
        count += proto::kOneOsdRow;
    }
    return settle_(before, frame.prefix(count));
}

bool LwOsdEndpoint::store_dirty_(proto::OsdRowCount n) noexcept {
    const hal::OsdStatus before = status_();
    Frame frame{};
    proto::OsdRowCount count{};
    for (proto::OsdRow r{}; n.covers(r); r += proto::kOneOsdRow) {
        if (w_.surface.seq(r) == stored_[r] || !store_row_(r))
            continue;
        frame[count.end()] = r;
        count += proto::kOneOsdRow;
    }
    return settle_(before, frame.prefix(count));
}

bool LwOsdEndpoint::settle_(hal::OsdStatus before, std::span<const proto::OsdRow> frame) noexcept {
    const hal::OsdDrops stored = status_().drops();

    if (stored != before.drops()) {
        ++tally_.restores;
        for (const proto::OsdRow r : frame)
            (void)store_row_(r);
    }

    const std::optional<hal::OsdStatus> drained = drain_();
    if (drained && drained->drops() == stored)
        return true;
    stored_ = {};
    return false;
}

bool LwOsdEndpoint::store_row_(proto::OsdRow r) noexcept {
    infra::CellGen g = w_.surface.seq(r);
    if (!g.held()) {
        row_ = proto::OsdSurface::Row{};
    } else {
        const std::uint32_t sampled = w_.surface.sample(r, row_);
        if (sampled == 0)
            return false;
        g = infra::CellGen{sampled};
    }
    Port& port = port_();
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    const std::span<const std::uint8_t> bytes{row_.b};
    const ImageRow& regs = kImageRegs[r];
    for (std::uint32_t i = 0; i < hal::OsdBlock::kRowWords; ++i) {
        const auto w = infra::load_le<std::uint32_t>(
            bytes.subspan(kWordBytes * i).first<kWordBytes>()
        );
        port.write(regs[i], hal::RegWord{w});
    }
    stored_[r] = g;
    ++tally_.rows;
    return true;
}

hal::OsdStatus LwOsdEndpoint::status_() noexcept {
    return hal::OsdStatus::of(port_().read(hal::OsdBlock::Reg::Status));
}

std::optional<hal::OsdStatus> LwOsdEndpoint::drain_() noexcept {
    for (unsigned poll = 0; poll < kDrainPolls; ++poll)
        if (const hal::OsdStatus s = status_(); s.drained())
            return s;
    return std::nullopt;
}

proto::OsdRowCount LwOsdEndpoint::bound_rows_() const noexcept {
    const proto::OsdRowCount visible = w_.surface.visible_rows();
    return visible < image_rows_ ? visible : image_rows_;
}

LwOsdEndpoint::Port& LwOsdEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

template class GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;

}

namespace mister::hal {
template class WindowPort<OsdBlock, seat_owner::Ui>;
}

namespace mister::infra::xthread {
template class GrantLease<app::OsdGrant, seat_owner::Ui>;
}
