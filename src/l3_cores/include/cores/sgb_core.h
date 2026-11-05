// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/core_support.h"
#include "cores/mailbox_carriage.h"
#include "cores/stream_load_host.h"
#include "infra/seat.h"
#include "proto/mailbox_frame_decl.h"

namespace mister::cores {

class SgbCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    SgbCore(const CoreProfile& p, const HostServices& h, proto::MailboxFrameDecl frame)
        : Core(p, h),
          mailbox_(MailboxCarriage::granted(h, frame)) {}

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    void on_set_pending_file_ext(std::string_view ext) noexcept override {
        stream_.set_pending_ext(ext);
    }

    std::uint64_t on_last_tx_bytes() const noexcept override { return stream_.bytes(); }

    svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override {
        return stream_.mount(slot, p);
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    StreamLoadHost stream_;
    std::optional<MailboxCarriage> mailbox_;
};

}
