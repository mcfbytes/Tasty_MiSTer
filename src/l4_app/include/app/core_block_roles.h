// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_support.h"
#include "cores/save_channel.h"
#include "infra/opt_ref.h"
#include "proto/block_backing.h"
#include "proto/block_roles.h"

namespace mister::app {

class CoreBlockRoles final : public proto::IBlockRoles {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit CoreBlockRoles(cores::Core& core) noexcept : core_(core) {}

    [[nodiscard]] proto::BlockBacking resolve(proto::SlotServer kind) noexcept override {
        TASTY_SEAT_BODY(CoreBlockRoles);
        switch (kind) {
            case proto::SlotServer::CoreBlock:
                if (const auto block_server = core_.block_server())
                    return proto::BlockBacking::core_block(block_server);
                return {};
            case proto::SlotServer::CoreDescriptor:
                if (const auto channel = core_.save_channel())
                    return proto::BlockBacking::core_descriptor(
                        infra::OptRef<proto::IImageSource>{channel->descriptor()}
                    );
                return {};
            case proto::SlotServer::Session:
            case proto::SlotServer::kCount:
                break;
        }
        return {};
    }

private:
    cores::Core& core_;
};

}
