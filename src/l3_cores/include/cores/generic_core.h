// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/core_grant.h"
#include "cores/core_support.h"
#include "cores/generic_profile.h"
#include "cores/stream_load.h"
#include "cores/stream_load_host.h"
#include "proto/download_session.h"
#include "infra/seat.h"

namespace mister::cores {

inline constexpr CoreProfile kMenuProfile{
    .kind = CoreKind::Menu,
    .name = "MENU",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},
    .is_front_end = true,
    .tasty_playable = true,
    .undeclared_start_mount = StartMount::Unmodelled,
};

class GenericCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    GenericCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    std::uint64_t bytes_sent() const noexcept { return stream_.bytes(); }

    svc::Crc32 tx_crc() const noexcept { return stream_.crc(); }

    std::uint32_t transfers() const noexcept { return stream_.transfers(); }

    std::uint32_t chunks() const noexcept { return stream_.chunks(); }

    std::uint32_t mount_refusals() const noexcept { return stream_.mount_refusals(); }

    std::uint16_t last_index() const noexcept { return stream_.last_index(); }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void on_set_pending_file_ext(std::string_view ext) noexcept override {
        stream_.set_pending_ext(ext);
    }

    std::uint64_t on_last_tx_bytes() const noexcept override { return stream_.bytes(); }

    svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    StreamLoadHost stream_;
};

std::unique_ptr<Core> make_generic(const CoreProfile& p, const CoreGrant& g);

}
