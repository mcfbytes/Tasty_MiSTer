// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/link_encoder.h"

namespace mister::app {

struct Delivered {
    ILinkEncoder::Outcome outcome = ILinkEncoder::Outcome::Dropped;
    bool round_spent = false;
};

}
