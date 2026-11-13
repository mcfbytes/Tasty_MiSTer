// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/fio_cheat_sink.h"
#include "cores/host_services.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/spi_image_sink.h"
#include "proto/types.h"
#include "reactor/core_state.h"
#include "reactor/core_token.h"
#include "svc/types.h"

namespace mister::proto {
class StatusBit;
class CoreSession;
class IBlockServer;
}

namespace mister::cores {

struct CoreAdopt;
class ISaveChannel;

class IStagingCore;
class ISlotMount;
class ICdServiceRows;
class ICdDiagnostics;
class IPcmWireRows;
class IMailboxRows;
class ICheatEngine;
class ISaveUpload;
class IStreamLoad;
class ISectorKick;
class IDipSwitches;
class IOptionRows;
class IConfigSlots;
class IWindowSave;

struct MountedPath;

class Core : public reactor::ICoreToken {
    TASTY_SEAT_RESIDENT(RT);

public:
    virtual ~Core() = default;

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    [[nodiscard]] Ex<void> adopt(const CoreAdopt& a);

    void retire() noexcept { do_retire(); }

    [[nodiscard]] Ex<std::optional<proto::StatusBit>> reset(const proto::ResetEdge& edge);
    [[nodiscard]] Ex<void> shutdown();

    const CoreProfile& profile() const noexcept { return *profile_; }

    std::span<const LinkDecoderDecl> services() const noexcept { return profile_->services; }

    [[nodiscard]] Ex<void> mount(IoIndex slot, const MountedPath& p);
    [[nodiscard]] Ex<void> file_tx(IoIndex index, std::span<const std::uint8_t> bytes);

    [[nodiscard]] Ex<void> osd_closed();

    void status_changed() noexcept { on_status_changed(); }

    void set_pending_file_ext(std::string_view ext) noexcept { on_set_pending_file_ext(ext); }

    void set_pending_file_path(std::string_view path) noexcept { on_set_pending_file_path(path); }

    [[nodiscard]] std::uint64_t last_tx_bytes() const noexcept { return on_last_tx_bytes(); }

    [[nodiscard]] svc::Crc32 last_tx_crc() const noexcept { return on_last_tx_crc(); }

    [[nodiscard]] Ex<void> apply_load_facts(
        std::span<const std::uint8_t> facts,
        std::string_view image
    ) {
        return on_apply_load_facts(facts, image);
    }

    [[nodiscard]] infra::OptRef<IStagingCore> staging_core() noexcept { return on_staging_core(); }

    [[nodiscard]] infra::OptRef<ISlotMount> slot_mount() noexcept { return on_slot_mount(); }

    [[nodiscard]] infra::OptRef<ICdServiceRows> cd_service_rows() noexcept {
        return on_cd_service_rows();
    }

    [[nodiscard]] infra::OptRef<IPcmWireRows> pcm_wire_rows() noexcept {
        return on_pcm_wire_rows();
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> mailbox_rows() noexcept { return on_mailbox_rows(); }

    [[nodiscard]] infra::OptRef<const ICdDiagnostics> cd_diagnostics() const noexcept {
        return on_cd_diagnostics();
    }

    [[nodiscard]] infra::OptRef<ISaveUpload> save_upload() noexcept { return on_save_upload(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> stream_load() noexcept { return on_stream_load(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> window_load() noexcept { return on_window_load(); }

    [[nodiscard]] infra::OptRef<ISectorKick> sector_kick() noexcept { return on_sector_kick(); }

    [[nodiscard]] infra::OptRef<IDipSwitches> dip_switches() noexcept { return on_dip_switches(); }

    [[nodiscard]] infra::OptRef<IConfigSlots> config_slots() noexcept { return on_config_slots(); }

    [[nodiscard]] infra::OptRef<IOptionRows> options() noexcept { return on_options(); }

    [[nodiscard]] ICheatSink& cheat_sink() noexcept { return on_cheat_sink(); }

    [[nodiscard]] infra::OptRef<ICheatEngine> cheat_engine() noexcept { return on_cheat_engine(); }

    [[nodiscard]] infra::OptRef<proto::IBlockServer> block_server() noexcept {
        return on_block_server();
    }

    [[nodiscard]] infra::OptRef<ISaveChannel> save_channel() noexcept { return on_save_channel(); }

    [[nodiscard]] infra::OptRef<IWindowSave> window_save() noexcept { return on_window_save(); }

    [[nodiscard]] proto::IImageSink& image_sink() noexcept {
        return host_.bulk ? *host_.bulk : spi_sink_;
    }

protected:
    Core(const CoreProfile& p, const HostServices& h)
        : profile_(&p),
          host_(h),
          spi_sink_(h.link, fio_queue_of_(h)),
          fio_cheats_(h.link, fio_queue_of_(h)) {}

    [[nodiscard]] static infra::OptRef<proto::SpiFioQueue> fio_queue_of_(
        const HostServices& h
    ) noexcept {
        return h.fio_queue;
    }

    const HostServices& host() const noexcept { return host_; }

private:
    [[nodiscard]] virtual Ex<void> do_adopt(const CoreAdopt& a) = 0;

    virtual void do_retire() noexcept {}

    [[nodiscard]] virtual Ex<void> do_reset() { return {}; }

    [[nodiscard]] virtual Ex<void> do_shutdown() { return {}; }

    [[nodiscard]] virtual Ex<void> on_mount(IoIndex, const MountedPath&) { return {}; }

    [[nodiscard]] virtual Ex<void> on_file_tx(IoIndex, std::span<const std::uint8_t>) {
        return unimplemented(ERR_SITE());
    }

    [[nodiscard]] virtual Ex<void> on_osd_closed() { return {}; }

    virtual void on_status_changed() noexcept {}

    virtual void on_set_pending_file_ext(std::string_view) noexcept {}

    virtual void on_set_pending_file_path(std::string_view) noexcept {}

    virtual std::uint64_t on_last_tx_bytes() const noexcept { return 0; }

    [[nodiscard]] virtual svc::Crc32 on_last_tx_crc() const noexcept { return {}; }

    [[nodiscard]] virtual Ex<void> on_apply_load_facts(
        std::span<const std::uint8_t>,
        std::string_view
    ) {
        return unimplemented(ERR_SITE());
    }

    [[nodiscard]] virtual infra::OptRef<IStagingCore> on_staging_core() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<ISlotMount> on_slot_mount() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<ICdServiceRows> on_cd_service_rows() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IPcmWireRows> on_pcm_wire_rows() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<const ICdDiagnostics> on_cd_diagnostics() const noexcept {
        return {};
    }

    [[nodiscard]] virtual infra::OptRef<ISaveUpload> on_save_upload() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IStreamLoad> on_stream_load() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IStreamLoad> on_window_load() noexcept {
        return on_stream_load();
    }

    [[nodiscard]] virtual infra::OptRef<ISectorKick> on_sector_kick() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IDipSwitches> on_dip_switches() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IConfigSlots> on_config_slots() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IOptionRows> on_options() noexcept { return {}; }

    [[nodiscard]] virtual ICheatSink& on_cheat_sink() noexcept { return fio_cheats_; }

    [[nodiscard]] virtual infra::OptRef<ICheatEngine> on_cheat_engine() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<proto::IBlockServer> on_block_server() noexcept {
        return {};
    }

    [[nodiscard]] virtual infra::OptRef<ISaveChannel> on_save_channel() noexcept { return {}; }

    [[nodiscard]] virtual infra::OptRef<IWindowSave> on_window_save() noexcept { return {}; }

    [[nodiscard]] Ex<void> pre_adopt_(proto::CoreSession& s);
    [[nodiscard]] Ex<void> flush_dirty_state_();

    const CoreProfile* profile_;
    HostServices host_;
    proto::SpiImageSink spi_sink_;
    FioCheatSink fio_cheats_;
    bool adopted_ = false;
};

[[nodiscard]] inline infra::OptRef<Core> bound_core(reactor::CoreState& st) noexcept {
    if (!st.owner)
        return {};
    return infra::OptRef<Core>{static_cast<Core&>(*st.owner)};
}

template <const CoreProfile& kProfile>
[[nodiscard]] inline infra::OptRef<Core> bound_core(reactor::CoreState& st) noexcept {
    const infra::OptRef<Core> c = bound_core(st);
    return (c && &c->profile() == &kProfile) ? c : infra::OptRef<Core>{};
}

}
