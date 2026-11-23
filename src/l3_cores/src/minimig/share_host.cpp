// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/share_host.h"

#include <cstdint>

#include "cores/minimig/share_wire.h"
#include "infra/endian.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

constexpr infra::ByteOffset kAckAt = kShareFlagAt + infra::ByteCount{sizeof(std::uint16_t)};

}

Ex<void> ShareHost::bind(std::span<std::byte> page) {
    TASTY_SEAT_BODY(ShareHost);
    page_ = {};
    if (infra::ByteCount{page.size()} < kSharePageBytes) {
        server_.unbind();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    }
    if (auto r = server_.bind_page(page); !r)
        return r;
    page_ = infra::affine::first(page, kSharePageBytes);

    seen_ = ShareRequestWord{infra::load_le<std::uint32_t>(infra::affine::from(page_, kShareFlagAt)
                                                               .first<sizeof(std::uint32_t)>())}
                .req_id();
    return {};
}

void ShareHost::unbind() noexcept {
    TASTY_SEAT_BODY(ShareHost);
    server_.unbind();
    page_ = {};
}

void ShareHost::reset() noexcept {
    TASTY_SEAT_BODY(ShareHost);
    server_.protocol().reset();
}

void ShareHost::serve() noexcept {
    TASTY_SEAT_BODY(ShareHost);
    if (page_.empty())
        return;
    const ShareRequestWord word{infra::load_le<std::uint32_t>(
        infra::affine::from(page_, kShareFlagAt).first<sizeof(std::uint32_t)>()
    )};
    if (word.req_id() == seen_)
        return;
    seen_ = word.req_id();
    if (!word.whole()) {
        tally_.partial.add();
        return;
    }
    const std::span<std::byte> packet = infra::affine::first(
        infra::affine::from(page_, kSharePacketAt),
        kSharePacketBytes
    );
    if (!server_.serve_request(packet, packet)) {
        tally_.refused.add();
        return;
    }
    tally_.served.add();

    word.store_ack(infra::affine::from(page_, kAckAt).first<sizeof(std::uint16_t)>());
}

}
