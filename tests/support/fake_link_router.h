// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "infra/error.h"
#include "proto/link_event.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::testing {

struct FakeLinkRouter final : mister::proto::ILinkRouter {
    using LinkEvent = mister::proto::LinkEvent;

    std::array<LinkEvent, 64> log{};
    std::size_t n = 0;
    bool accept = true;
    bool room = true;
    std::size_t max_ask = SIZE_MAX;
    std::uint16_t next_id = 1;
    mutable LinkEvent::Kind asked{};
    LinkEvent::Kind interned{};
    std::string interned_bytes;

    bool push(const LinkEvent& e) noexcept override {
        if (!accept || n == log.size())
            return false;
        log[n++] = e;
        return true;
    }

    bool can_intern(LinkEvent::Kind k, std::size_t bytes, unsigned) const noexcept override {
        asked = k;
        return room && bytes <= max_ask;
    }

    mister::Ex<mister::proto::RxSlabId> intern(
        LinkEvent::Kind k,
        std::span<const std::uint8_t> b
    ) noexcept override {
        interned = k;
        if (!room) {
            return std::unexpected(mister::Error{mister::Errc::would_block, ERR_SITE(), 0});
        }
        interned_bytes.append(reinterpret_cast<const char*>(b.data()), b.size());
        return mister::proto::RxSlabId{next_id++};
    }
};

}
