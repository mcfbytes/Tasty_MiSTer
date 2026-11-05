// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link.h"
#include "proto/save_request.h"

namespace mister::proto {

class LinkOps final {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit LinkOps(Link& link) noexcept : link_(link) {}

    LinkOps(const LinkOps&) = delete;
    LinkOps& operator=(const LinkOps&) = delete;

    [[nodiscard]] Ex<SaveRequestWord> read_save_request();

private:
    Link& link_;
};

}
