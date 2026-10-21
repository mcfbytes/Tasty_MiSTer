// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/core_grant.h"
#include "cores/core_support.h"
#include "cores/generic_profile.h"
#include "cores/stream_load.h"
#include "cores/stream_tx_record.h"
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
    .undeclared_start_mount = StartMount::Unmodelled,
};

class GenericCore final : public Core, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    GenericCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    static constexpr std::size_t kChunkBytes = 4096;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    void set_pending_ext(std::string_view ext) noexcept;

    std::uint64_t bytes_sent() const noexcept { return tx_.bytes(); }

    std::uint32_t tx_crc() const noexcept { return tx_.crc(); }

    std::uint32_t transfers() const noexcept { return transfers_; }

    std::uint32_t chunks() const noexcept { return chunks_; }

    std::uint32_t mount_refusals() const noexcept { return mount_refusals_; }

    std::uint16_t last_index() const noexcept { return last_index_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void on_set_pending_file_ext(std::string_view ext) noexcept override { set_pending_ext(ext); }

    std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent(); }

    std::uint32_t on_last_tx_crc() const noexcept override { return tx_.crc(); }

    IStreamLoad* on_stream_load() noexcept override { return this; }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    StreamTxRecord tx_;
    std::uint32_t transfers_ = 0;
    std::uint32_t chunks_ = 0;
    std::uint32_t mount_refusals_ = 0;
    std::uint16_t last_index_ = 0;
};

std::unique_ptr<Core> make_generic(const CoreProfile& p, const CoreGrant& g);

}
