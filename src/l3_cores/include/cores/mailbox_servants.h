// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cores/mailbox_servant.h"
#include "svc/vfs.h"

namespace mister::cores {

enum class ServantId : std::uint8_t { None, Msu1, kCount };

using ServantSet =
    std::array<std::unique_ptr<IMailboxServant>, static_cast<std::size_t>(ServantId::kCount)>;

[[nodiscard]] ServantSet make_servants(const svc::Vfs& vfs);

}  // namespace mister::cores
