// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/share_lane.h"

#include <span>
#include <utility>

#include "infra/prelude.h"

namespace mister::app {

void ShareLane::serve() noexcept {
    TASTY_SEAT_BODY(ShareLane);
    if (reader_.take_if_changed(w_.grant, incoming_))
        apply_(incoming_);
    if (!host_)
        return;
    host_->serve();
    tally_.served.add();
}

infra::ParkTimeout ShareLane::poll_period() const noexcept {
    TASTY_SEAT_BODY(ShareLane);
    return poll_;
}

void ShareLane::apply_(const ShareGrant& g) noexcept {
    if (!g.want) {
        close_();
        return;
    }
    const ShareGrant::Open& o = *g.want;
    if (!applied_ || o.open != applied_->open) {
        open_(o);
        return;
    }
    if (o.reset == applied_->reset)
        return;
    applied_ = o;
    if (!host_)
        return;
    host_->reset();
    tally_.resets.add();
}

void ShareLane::open_(const ShareGrant::Open& o) noexcept {
    close_();
    applied_ = o;
    if (o.make == nullptr) {
        tally_.refused.add();
        return;
    }
    std::unique_ptr<cores::IShareHost> host = o.make(w_.vfs, o.root.view());
    if (!host) {
        tally_.refused.add();
        return;
    }
    const auto at = w_.aperture.physical_of(o.page);
    if (!at) {
        tally_.refused.add();
        return;
    }
    const auto region = w_.aperture.resolve(*at, o.bytes.file_word<std::uint64_t>(), "share-page");
    if (!region) {
        tally_.refused.add();
        return;
    }
    auto page = w_.map.map(*region);
    if (!page) {
        tally_.refused.add();
        return;
    }
    if (auto bound = host->bind(page->view(0, page->region().len)); !bound) {
        tally_.refused.add();
        return;
    }
    page_.emplace(std::move(*page));
    host_ = std::move(host);
    poll_ = o.poll;
    tally_.opened.add();
}

void ShareLane::close_() noexcept {
    applied_.reset();
    poll_ = infra::kForever;
    if (!host_)
        return;
    host_->unbind();
    host_.reset();
    page_.reset();
    tally_.closed.add();
}

}
