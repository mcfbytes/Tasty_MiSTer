// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_router.h"

#include <expected>

#include "app/link_rx_channel.h"

namespace mister::app {

static_assert(proto::ILinkRouter::kChunkBytes == kLinkRxSlabBytes,
              "the chunk width handed to producers is the slab's slot width");

LinkRouter::LinkRouter(const Channels& c) noexcept : main_(c.main), ui_(c.ui), input_(c.input) {}

bool LinkRouter::push(const proto::LinkEvent& e) noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    bool pushed = false;
    switch (sink_for(e)) {
        case EventSink::Main:
            pushed = main_.push(e);
            break;
        case EventSink::Ui:
            pushed = ui_.push(e);
            break;
        case EventSink::Input:
            pushed = input_ && input_->push(e);
            break;
        case EventSink::kCount:
            break;
    }
    if (!pushed) ++drops_;
    return pushed;
}

bool LinkRouter::can_intern(proto::LinkEvent::Kind k, std::size_t bytes,
                            unsigned chunks) const noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    switch (sink_for(k)) {
        case EventSink::Main:
            return main_.can_intern(bytes, chunks);
        case EventSink::Ui:
            return ui_.can_intern(bytes, chunks);
        case EventSink::Input:
            return input_ && input_->can_intern(bytes, chunks);
        case EventSink::kCount:
            break;
    }
    return false;
}

Ex<proto::RxSlabId> LinkRouter::intern(proto::LinkEvent::Kind k,
                                       std::span<const std::uint8_t> bytes) noexcept {
    TASTY_SEAT_BODY(LinkRouter);
    Ex<proto::RxSlabId> id = std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    switch (sink_for(k)) {
        case EventSink::Main:
            id = main_.intern(bytes);
            break;
        case EventSink::Ui:
            id = ui_.intern(bytes);
            break;
        case EventSink::Input:
            if (input_) id = input_->intern(bytes);
            break;
        case EventSink::kCount:
            break;
    }
    if (!id) ++intern_refusals_;
    return id;
}

}  // namespace mister::app
