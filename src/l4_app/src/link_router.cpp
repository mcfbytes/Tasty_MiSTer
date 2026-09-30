// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_router.h"

#include <expected>

#include "app/link_rx_channel.h"

namespace mister::app {

static_assert(proto::ILinkRouter::kChunkBytes == LinkRxChannel::Bytes::kBytes,
              "the chunk width handed to producers is the slab's slot width");

LinkRouter::LinkRouter(const Channels& c) noexcept {
    chans_[infra::ordinal(EventSink::Main)] = c.main;
    chans_[infra::ordinal(EventSink::Ui)] = c.ui;
    chans_[infra::ordinal(EventSink::Input)] = c.input;
}

LinkRxChannel* LinkRouter::channel_(proto::LinkEvent::Kind k) const noexcept {
    const std::size_t i = infra::ordinal(sink_for(k));
    return i < chans_.size() ? chans_[i] : nullptr;
}

bool LinkRouter::push(const proto::LinkEvent& e) noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    LinkRxChannel* const c = channel_(infra::kind_of(e));
    if (c == nullptr || !c->push(e)) {
        ++drops_;
        return false;
    }
    return true;
}

bool LinkRouter::can_intern(proto::LinkEvent::Kind k, std::size_t bytes,
                            unsigned chunks) const noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    const LinkRxChannel* const c = channel_(k);
    return c != nullptr && c->can_intern(bytes, chunks);
}

Ex<proto::RxSlabId> LinkRouter::intern(proto::LinkEvent::Kind k,
                                       std::span<const std::uint8_t> bytes) noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    LinkRxChannel* const c = channel_(k);
    if (c == nullptr) {
        ++intern_refusals_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    auto id = c->intern(bytes);
    if (!id) ++intern_refusals_;
    return id;
}

}  // namespace mister::app
