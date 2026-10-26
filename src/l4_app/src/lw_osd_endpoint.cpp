// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_osd_endpoint.h"

#include <algorithm>
#include <cstddef>

#include "infra/lease_order.h"
#include "proto/types.h"

namespace mister::app {

namespace {

constexpr unsigned kLowresRows = 8;

}

void LwOsdEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(LwOsdEndpoint);
    take_grant_();
    if (bound_gen_ == 0)
        return;
    store_frame_();
}

void LwOsdEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(LwOsdEndpoint);
    unbind_();
}

void LwOsdEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(LwOsdEndpoint);
    unbind_();
}

void LwOsdEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(LwOsdEndpoint);
    grant_rd_.forget();
}

void LwOsdEndpoint::take_grant_() noexcept {
    const auto g = grant_rd_.take_if_changed(w_.grant);
    if (!g)
        return;

    unbind_();
    if (g->live())
        bind_(*g, grant_rd_.seen());
}

void LwOsdEndpoint::bind_(const OsdGrant& g, std::uint32_t gen) noexcept {
    bound_gen_ = gen;
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
    if (!lease_())
        return;
    for (std::uint32_t r = 0; r < kRows; ++r)
        (void)store_row_(r);
    const std::uint32_t ctrl = ctrl_();
    port_().write(hal::OsdBlock::Reg::Ctrl, ctrl);
    (void)port_().read(hal::OsdBlock::Reg::Ctrl);
    ctrl_stored_ = ctrl;
    tally_.ctrl = ctrl;
    release_();
    tally_.on_lw = 1;
    ++tally_.frames;
    publish_();
}

void LwOsdEndpoint::unbind_() noexcept {
    if (bound_gen_ == 0)
        return;
    block_.reset();
    bound_gen_ = 0;
    stored_ = {};
    ctrl_stored_ = 0;
    tally_.on_lw = 0;
    publish_();
}

void LwOsdEndpoint::store_frame_() noexcept {
    const std::uint32_t n = bound_rows_();
    const std::uint32_t ctrl = ctrl_();
    bool dirty = ctrl != ctrl_stored_;
    for (std::uint32_t r = 0; r < n && !dirty; ++r)
        dirty = w_.surface.seq(proto::OsdRow{static_cast<std::uint8_t>(r)}) != stored_[r];
    if (!dirty)
        return;
    if (!lease_())
        return;
    for (std::uint32_t r = 0; r < n; ++r) {
        if (w_.surface.seq(proto::OsdRow{static_cast<std::uint8_t>(r)}) != stored_[r])
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

bool LwOsdEndpoint::store_row_(std::uint32_t row) noexcept {
    const proto::OsdRow r{static_cast<std::uint8_t>(row)};
    std::uint32_t g = w_.surface.seq(r);
    if (g == 0) {
        row_ = proto::OsdSurface::Row{};
    } else {
        g = w_.surface.sample(r, row_);
        if (g == 0)
            return false;
    }
    Port& port = port_();
    for (std::uint32_t i = 0; i < hal::OsdBlock::kRowWords; ++i) {
        const std::uint8_t* b = row_.b + 4u * i;
        const std::uint32_t w = std::uint32_t{b[0]}
            | (std::uint32_t{b[1]} << 8)
            | (std::uint32_t{b[2]} << 16)
            | (std::uint32_t{b[3]} << 24);
        port.write(hal::OsdBlock::image(row, i), w);
    }
    stored_[row] = g;
    ++tally_.rows;
    return true;
}

std::uint32_t LwOsdEndpoint::bound_rows_() const noexcept {
    return std::min<std::uint32_t>(kRows, w_.surface.visible_rows());
}

std::uint32_t LwOsdEndpoint::ctrl_() const noexcept {
    const bool hr = w_.surface.visible_rows() > kLowresRows;
    return hal::OsdBlock::kCtrlLwSel | (hr ? hal::OsdBlock::kCtrlHr : 0u);
}

bool LwOsdEndpoint::lease_() noexcept {
    tally_.leased = 1;
    publish_();
    if (xthread::LeaseOrder::grant_unchanged(w_.grant, bound_gen_))
        return true;
    tally_.leased = 0;
    ++tally_.revoked;
    unbind_();
    return false;
}

void LwOsdEndpoint::release_() noexcept {
    tally_.leased = 0;
    publish_();
}

void LwOsdEndpoint::publish_() noexcept {
    w_.route.publish(tally_);
}

LwOsdEndpoint::Port& LwOsdEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

}
