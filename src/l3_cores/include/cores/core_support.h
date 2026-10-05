// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/button_override.h"
#include "cores/cheat_records.h"
#include "cores/core_profile.h"
#include "cores/fio_cheat_sink.h"
#include "cores/host_services.h"
#include "cores/save_channel.h"
#include "infra/error.h"
#include "os/clock.h"
#include "reactor/core_state.h"
#include "reactor/core_token.h"
#include "proto/block_geometry_hook.h"
#include "proto/resident_image_source.h"
#include "proto/core_session.h"
#include "proto/image_sink.h"
#include "proto/spi_image_sink.h"
#include "proto/types.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::cores {

class IStagingCore;
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

    [[nodiscard]] Ex<void> init(proto::CoreSession& s);

    [[nodiscard]] Ex<std::optional<proto::StatusBit>> reset(const proto::ResetEdge& edge);
    [[nodiscard]] Ex<void> shutdown();

    const CoreProfile& profile() const noexcept { return *profile_; }
    std::span<const LinkDecoderDecl> services() const noexcept { return profile_->services; }

    [[nodiscard]] Ex<void> mount(IoIndex slot, const MountedPath& p);
    [[nodiscard]] Ex<void> file_tx(IoIndex index, svc::IFile& f);

    [[nodiscard]] Ex<void> osd_closed();

    void set_pending_file_ext(std::string_view ext) noexcept { on_set_pending_file_ext(ext); }

    void set_pending_file_path(std::string_view path) noexcept { on_set_pending_file_path(path); }
    [[nodiscard]] std::uint64_t last_tx_bytes() const noexcept { return on_last_tx_bytes(); }

    [[nodiscard]] std::uint32_t last_tx_crc() const noexcept { return on_last_tx_crc(); }

    [[nodiscard]] Ex<void> apply_load_facts(std::span<const std::uint8_t> facts) {
        return on_apply_load_facts(facts);
    }

    void set_manifest_path(std::string_view path) { on_set_manifest_path(path); }

    void set_manifest_text(std::string_view text) { on_set_manifest_text(text); }
    [[nodiscard]] std::string_view manifest_error() const noexcept { return on_manifest_error(); }

    [[nodiscard]] ButtonOverride button_override() const noexcept { return on_button_override(); }
    [[nodiscard]] IStagingCore* staging_core() noexcept { return on_staging_core(); }
    [[nodiscard]] ICdServiceRows* cd_service_rows() noexcept { return on_cd_service_rows(); }

    [[nodiscard]] IPcmWireRows* pcm_wire_rows() noexcept { return on_pcm_wire_rows(); }

    [[nodiscard]] IMailboxRows* mailbox_rows() noexcept { return on_mailbox_rows(); }
    [[nodiscard]] const ICdDiagnostics* cd_diagnostics() const noexcept {
        return on_cd_diagnostics();
    }

    [[nodiscard]] ISaveUpload* save_upload() noexcept { return on_save_upload(); }

    [[nodiscard]] IStreamLoad* stream_load() noexcept { return on_stream_load(); }

    [[nodiscard]] IStreamLoad* window_load() noexcept { return on_window_load(); }

    [[nodiscard]] ISectorKick* sector_kick() noexcept { return on_sector_kick(); }
    [[nodiscard]] IDipSwitches* dip_switches() noexcept { return on_dip_switches(); }

    [[nodiscard]] IConfigSlots* config_slots() noexcept { return on_config_slots(); }

    [[nodiscard]] IOptionRows* options() noexcept { return on_options(); }

    [[nodiscard]] ICheatSink* cheat_sink() noexcept { return on_cheat_sink(); }
    [[nodiscard]] ICheatRecords* cheat_records() noexcept { return on_cheat_records(); }

    [[nodiscard]] ICheatEngine* cheat_engine() noexcept { return on_cheat_engine(); }
    [[nodiscard]] proto::IResidentImageSource* block_source() noexcept { return on_block_source(); }

    [[nodiscard]] ISaveChannel* save_channel() noexcept { return on_save_channel(); }
    [[nodiscard]] proto::IBlockGeometry* block_geometry() noexcept { return on_block_geometry(); }

    [[nodiscard]] IWindowSave* window_save() noexcept { return on_window_save(); }

    [[nodiscard]] proto::IImageSink& image_sink() noexcept {
        return host_.bulk != nullptr ? *host_.bulk : spi_sink_;
    }

protected:
    Core(const CoreProfile& p, const HostServices& h)
        : profile_(&p), host_(h), spi_sink_(h.link, h.fio_queue), fio_cheats_(h.link, h.fio_queue) {
    }

    const HostServices& host() const noexcept { return host_; }

private:
    [[nodiscard]] virtual Ex<void> do_init(proto::CoreSession& s) = 0;
    [[nodiscard]] virtual Ex<void> do_reset() { return {}; }
    [[nodiscard]] virtual Ex<void> do_shutdown() { return {}; }
    [[nodiscard]] virtual Ex<void> on_mount(IoIndex, const MountedPath&) { return {}; }

    [[nodiscard]] virtual Ex<void> on_file_tx(IoIndex, svc::IFile&) {
        return unimplemented(ERR_SITE());
    }
    [[nodiscard]] virtual Ex<void> on_osd_closed() { return {}; }
    virtual void on_set_pending_file_ext(std::string_view) noexcept {}
    virtual void on_set_pending_file_path(std::string_view) noexcept {}
    virtual std::uint64_t on_last_tx_bytes() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint32_t on_last_tx_crc() const noexcept { return 0; }
    [[nodiscard]] virtual Ex<void> on_apply_load_facts(std::span<const std::uint8_t>) {
        return unimplemented(ERR_SITE());
    }
    virtual void on_set_manifest_path(std::string_view) {}
    virtual void on_set_manifest_text(std::string_view) {}
    [[nodiscard]] virtual std::string_view on_manifest_error() const noexcept { return {}; }

    [[nodiscard]] virtual ButtonOverride on_button_override() const noexcept { return {}; }
    [[nodiscard]] virtual IStagingCore* on_staging_core() noexcept { return nullptr; }
    [[nodiscard]] virtual ICdServiceRows* on_cd_service_rows() noexcept { return nullptr; }
    [[nodiscard]] virtual IPcmWireRows* on_pcm_wire_rows() noexcept { return nullptr; }
    [[nodiscard]] virtual IMailboxRows* on_mailbox_rows() noexcept { return nullptr; }
    [[nodiscard]] virtual const ICdDiagnostics* on_cd_diagnostics() const noexcept {
        return nullptr;
    }
    [[nodiscard]] virtual ISaveUpload* on_save_upload() noexcept { return nullptr; }
    [[nodiscard]] virtual IStreamLoad* on_stream_load() noexcept { return nullptr; }
    [[nodiscard]] virtual IStreamLoad* on_window_load() noexcept { return on_stream_load(); }
    [[nodiscard]] virtual ISectorKick* on_sector_kick() noexcept { return nullptr; }
    [[nodiscard]] virtual IDipSwitches* on_dip_switches() noexcept { return nullptr; }
    [[nodiscard]] virtual IConfigSlots* on_config_slots() noexcept { return nullptr; }
    [[nodiscard]] virtual IOptionRows* on_options() noexcept { return nullptr; }
    [[nodiscard]] virtual ICheatSink* on_cheat_sink() noexcept { return &fio_cheats_; }
    [[nodiscard]] virtual ICheatRecords* on_cheat_records() noexcept { return nullptr; }
    [[nodiscard]] virtual ICheatEngine* on_cheat_engine() noexcept { return nullptr; }
    [[nodiscard]] virtual proto::IResidentImageSource* on_block_source() noexcept {
        return nullptr;
    }
    [[nodiscard]] virtual ISaveChannel* on_save_channel() noexcept { return nullptr; }
    [[nodiscard]] virtual proto::IBlockGeometry* on_block_geometry() noexcept { return nullptr; }
    [[nodiscard]] virtual IWindowSave* on_window_save() noexcept { return nullptr; }

    [[nodiscard]] Ex<void> pre_init_(proto::CoreSession& s);
    [[nodiscard]] Ex<void> flush_dirty_state_();

    const CoreProfile* profile_;
    HostServices host_;
    proto::SpiImageSink spi_sink_;
    FioCheatSink fio_cheats_;
    bool inited_ = false;
};

[[nodiscard]] inline Core* bound_core(reactor::CoreState& st) noexcept {
    return static_cast<Core*>(st.owner);
}

}  // namespace mister::cores
