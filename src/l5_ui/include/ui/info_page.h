// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

#include "app/event.h"
#include "ui/page.h"

namespace mister::ui {

std::unique_ptr<Page> make_info_page(app::InfoId id, std::uint32_t timeout_ms);

}
