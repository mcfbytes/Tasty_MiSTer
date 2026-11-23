// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_share_host.h"

#include <cstdint>

#include "infra/span_cut.h"

namespace mister::cores::x86 {

namespace {

constexpr infra::ByteOffset kAckAt{2};

}

Ex<void> ShareHost::bind(std::span<std::byte> page) {
    TASTY_SEAT_BODY(ShareHost);
    page_ = {};
    if (auto bound = server_.bind_page(page); !bound)
        return bound;
    page_ = page;

    (void)server_.sandbox().make_root();
    seen_ = DosShareWord::load(page_.first<sizeof(std::uint32_t)>()).req_id();
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
    const DosShareWord word = DosShareWord::load(page_.first<sizeof(std::uint32_t)>());
    if (word.req_id() == seen_)
        return;
    seen_ = word.req_id();
    if (!word.whole()) {
        tally_.partial.add();
        return;
    }
    const std::span<std::byte> packet = infra::affine::from(page_, DosShare::kRequestAt);
    if (!server_.serve_request(packet, packet)) {
        tally_.refused.add();
        return;
    }
    tally_.served.add();
    word.store_ack(infra::affine::from(page_, kAckAt).first<2>());
}

}
