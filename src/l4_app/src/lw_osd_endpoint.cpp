// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_osd_endpoint.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "infra/endian.h"
#include "proto/types.h"

namespace mister::app {

namespace {

constexpr proto::OsdRowCount kLowresRows{8};

using ImageRow = std::array<hal::OsdBlock::Reg, hal::OsdBlock::kRowWords>;
constexpr proto::OsdRowCells<ImageRow, hal::OsdBlock::kImageRows> kImageRegs = [] {
    proto::OsdRowCells<ImageRow, hal::OsdBlock::kImageRows> regs{};
    for (std::size_t row = 0; row < regs.size(); ++row)
        for (std::uint32_t w = 0; w < hal::OsdBlock::kRowWords; ++w)
            regs[row][w] = hal::OsdBlock::image(row, w);
    return regs;
}();

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

void LwOsdEndpoint::bind_hook_(const OsdGrant&) noexcept {
    for (proto::OsdRow r{}; kRows.covers(r); r += proto::kOneOsdRow)
        (void)store_row_(r);
    const std::uint32_t ctrl = ctrl_();
    port_().write(hal::OsdBlock::Reg::Ctrl, ctrl);
    (void)port_().read(hal::OsdBlock::Reg::Ctrl);
    ctrl_stored_ = ctrl;
    tally_.ctrl = ctrl;
}

void LwOsdEndpoint::bound_hook_() noexcept {
    ++tally_.frames;
    publish_();
}

void LwOsdEndpoint::answer_hook_(std::uint16_t) noexcept {}

void LwOsdEndpoint::unbind_hook_() noexcept {
    block_.reset();
    stored_ = {};
    ctrl_stored_ = 0;
}

void LwOsdEndpoint::serve_bound_() noexcept {
    store_frame_();
}

bool LwOsdEndpoint::idle_bound_hook_() const noexcept {
    return true;
}

void LwOsdEndpoint::store_frame_() noexcept {
    const proto::OsdRowCount n = bound_rows_();
    const std::uint32_t ctrl = ctrl_();
    bool dirty = ctrl != ctrl_stored_;
    for (proto::OsdRow r{}; n.covers(r) && !dirty; r += proto::kOneOsdRow)
        dirty = w_.surface.seq(r) != proto::row_cell(stored_, r);
    if (!dirty)
        return;
    if (!lease_())
        return;
    for (proto::OsdRow r{}; n.covers(r); r += proto::kOneOsdRow) {
        if (w_.surface.seq(r) != proto::row_cell(stored_, r))
            (void)store_row_(r);
    }
    if (ctrl != ctrl_stored_) {
        port_().write(hal::OsdBlock::Reg::Ctrl, ctrl);
        ctrl_stored_ = ctrl;
        tally_.ctrl = ctrl;
    }
    (void)port_().read(hal::OsdBlock::Reg::Ctrl);
    release_();
    ++tally_.frames;
    publish_();
}

bool LwOsdEndpoint::store_row_(proto::OsdRow r) noexcept {
    std::uint32_t g = w_.surface.seq(r);
    if (g == 0) {
        row_ = proto::OsdSurface::Row{};
    } else {
        g = w_.surface.sample(r, row_);
        if (g == 0)
            return false;
    }
    Port& port = port_();
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    const std::span<const std::uint8_t> bytes{row_.b};
    const ImageRow& regs = proto::row_cell(kImageRegs, r);
    for (std::uint32_t i = 0; i < hal::OsdBlock::kRowWords; ++i) {
        const auto w = infra::load_le<std::uint32_t>(
            bytes.subspan(kWordBytes * i).first<kWordBytes>()
        );
        port.write(regs[i], w);
    }
    proto::row_cell(stored_, r) = g;
    ++tally_.rows;
    return true;
}

proto::OsdRowCount LwOsdEndpoint::bound_rows_() const noexcept {
    const proto::OsdRowCount visible = w_.surface.visible_rows();
    constexpr proto::OsdRowCount page = kRows;
    return visible < page ? visible : page;
}

std::uint32_t LwOsdEndpoint::ctrl_() const noexcept {
    const bool hr = w_.surface.visible_rows() > kLowresRows;
    return hal::OsdBlock::kCtrlLwSel | (hr ? hal::OsdBlock::kCtrlHr : 0u);
}

LwOsdEndpoint::Port& LwOsdEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

}
