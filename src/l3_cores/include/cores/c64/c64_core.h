// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/c64/easyflash_image.h"
#include "cores/core_support.h"
#include "cores/nvram_save.h"
#include "cores/save_home.h"
#include "cores/save_shape.h"
#include "cores/save_upload.h"
#include "cores/stream_ask.h"
#include "cores/stream_load.h"
#include "cores/stream_load_end.h"
#include "cores/stream_load_host.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/path_text.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/reset_terms.h"
#include "proto/session_params.h"
#include "proto/status_bit.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::c64 {

inline constexpr proto::StatusBit kDetachResetBit{17};
inline constexpr proto::StatusBit kVideoStandardBit{2};

inline constexpr proto::ResetTerms kResetTerms = proto::ResetTerms::framework()
                                                     .with_held(kDetachResetBit)
                                                     .with_changed(kVideoStandardBit);

class C64Core final : public Core, public ISaveUpload, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    C64Core(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    [[nodiscard]] std::string_view save_file_name() const noexcept override;
    [[nodiscard]] std::optional<mra::NvramSave> save_image() const noexcept override;

    [[nodiscard]] SaveHome save_home() const noexcept override {
        TASTY_SEAT_BODY(C64Core);
        return SaveHome::CoreSaves;
    }

    [[nodiscard]] bool saved_with_settings() const noexcept override {
        TASTY_SEAT_BODY(C64Core);
        return false;
    }

    [[nodiscard]] SaveShape save_shape() const noexcept override;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(const StreamAsk& ask) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] bool holds_dump() const noexcept { return dump_.has_value(); }

    [[nodiscard]] infra::Tally facts_refusals() const noexcept { return facts_refusals_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    [[nodiscard]] Ex<void> on_apply_load_facts(
        std::span<const std::uint8_t> facts,
        std::string_view image
    ) override;
    [[nodiscard]] infra::OptRef<ISaveUpload> on_save_upload() noexcept override;

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{*this};
    }

    struct Dump {
        CrtHeader header{};
        infra::PathText save_name{};
    };

    StreamLoadHost stream_;
    std::optional<Dump> dump_{};
    infra::Tally facts_refusals_{};
};

}
