// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/save_channel.h"
#include "infra/opt_ref.h"
#include "proto/block_roles.h"

namespace mister::app {

class SaveChannelRoles final : public proto::IBlockRoles {
public:
    explicit SaveChannelRoles(cores::ISaveChannel& channel) noexcept : channel_(&channel) {}

    [[nodiscard]] proto::BlockBacking resolve(proto::SlotServer kind) noexcept override {
        if (kind == proto::SlotServer::CoreDescriptor)
            return proto::BlockBacking::core_descriptor(infra::opt_ref_of(&channel_->descriptor()));
        return {};
    }

private:
    cores::ISaveChannel* channel_;
};

}
