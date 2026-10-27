// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/core_support.h"
#include "cores/generic_core.h"
#include "cores/mailbox_carriage.h"
#include "infra/seat.h"
#include "proto/mailbox_frame_decl.h"

namespace mister::cores {

class SgbCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    SgbCore(const CoreProfile& p, const HostServices& h, proto::MailboxFrameDecl frame)
        : Core(p, h),
          file_host_(kGenericProfile, h),
          mailbox_(MailboxCarriage::granted(h, frame)) {}

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    void on_set_pending_file_ext(std::string_view ext) noexcept override {
        file_host_.set_pending_ext(ext);
    }

    std::uint64_t on_last_tx_bytes() const noexcept override { return file_host_.bytes_sent(); }

    std::uint32_t on_last_tx_crc() const noexcept override { return file_host_.tx_crc(); }

    IStreamLoad* on_stream_load() noexcept override { return &file_host_; }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override {
        return file_host_.mount(slot, p);
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    GenericCore file_host_;
    std::optional<MailboxCarriage> mailbox_;
};

}
