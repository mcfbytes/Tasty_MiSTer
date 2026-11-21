// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/mailbox_servant.h"
#include "cores/servant_grant.h"
#include "cores/servant_id.h"

namespace mister::cores {

using MakeMailboxServant = std::unique_ptr<IMailboxServant> (*)(const ServantGrant&);

struct MailboxServantRow {
    ServantId id = ServantId::None;
    MakeMailboxServant make = nullptr;

    [[nodiscard]] constexpr bool declared() const noexcept { return make != nullptr; }
};

}
