// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/cheat_catalog_cell.h"
#include "app/conf_str_cell.h"
#include "app/core_scope.h"
#include "app/intern_exact.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/mra_facts.h"
#include "app/path_text.h"
#include "app/bitstream_resolve.h"
#include "app/remembered_path.h"
#include "app/ui_request.h"
#include "app/xml_kind.h"
#include "cores/ram_image_recipe.h"
#include "hal/phys_region.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/core_session.h"
#include "proto/link_event.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {
struct CoreFactory;
struct CoreProfile;
}

namespace mister::app {

inline constexpr proto::FileSize kManifestDocMax{16u << 20};

inline constexpr std::size_t kCoreCfgBytes = 16;

class NegotiatedCore final {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Tally {
        std::uint32_t conf_str_parse_refusals = 0;
    };

    NegotiatedCore(
        const LinkRxChannel<SeatTag::Main>& rx,
        ConfStrCell& conf_str_cell,
        const svc::Vfs& vfs,
        LinkTxChannel<SeatTag::Main>& inbox
    ) noexcept
        : rx_(rx),
          conf_str_cell_(conf_str_cell),
          vfs_(vfs),
          inbox_(inbox) {}

    NegotiatedCore(const NegotiatedCore&) = delete;
    NegotiatedCore& operator=(const NegotiatedCore&) = delete;

    [[nodiscard]] Ex<std::reference_wrapper<const proto::ConfStr>> accept(
        const proto::LinkEvent::ConfStr& c,
        hal::PhysRegion aperture
    ) {
        TASTY_SEAT_BODY(NegotiatedCore);

        std::string raw;
        if (c.chunk0.v != 0) {
            const auto bytes = rx_.bytes(c.chunk0);
            raw.append(infra::as_text(bytes));
        }
        if (c.chunks == 2) {
            const auto bytes = rx_.bytes(c.chunk1);
            raw.append(infra::as_text(bytes));
        }

        ConfStrText body{};
        body.truncated = c.read == proto::LinkEvent::ConfStrRead::Truncated;
        body.gen = c.bind_gen;
        (void)body.text.assign(raw);
        conf_str_cell_.publish(body);

        conf_str_.reset();
        pending_ram_image_ = {};
        auto conf = proto::ConfStr::parse(raw, aperture);
        if (!conf) {
            ++tally_.conf_str_parse_refusals;
            return std::unexpected(conf.error());
        }
        conf_str_gen_ = c.bind_gen;
        const proto::ConfStr& cs = conf_str_.emplace(std::move(*conf));

        (void)core_name_.assign(cs.name());
        return std::cref(cs);
    }

    [[nodiscard]] proto::TxSlabId key_names(
        const proto::ConfStr& cs,
        const UiRequest::LoadCore& pending
    );

    void latch_mra_facts(const UiRequest::LoadCore& pending);

    void forget() noexcept;

    void hold_ram_image(const cores::RamImageRecipe& recipe) noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        pending_ram_image_ = recipe;
    }

    [[nodiscard]] cores::RamImageRecipe take_ram_image() noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return std::exchange(pending_ram_image_, {});
    }

    void clear_core_name() noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        core_name_.clear();
    }

    void note_build(std::string_view built_from);

    void note_made(bool made) noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        core_live_ = made;
    }

    void note_identity(const proto::LinkEvent::IdentityMatched& w) noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        generic_identity_ = proto::fold_core_type(w.type_byte) == proto::CoreType::EightBit;
    }

    void forget_identity() noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        generic_identity_ = false;
    }

    void note_width(bool wide) noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        link_wide_ = wide;
    }

    void facts_sent() noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        facts_pending_ = false;
    }

    [[nodiscard]] const std::optional<proto::ConfStr>& conf_str() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return conf_str_;
    }

    [[nodiscard]] CoreScope conf_scope() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return CoreScope{conf_str_gen_};
    }

    [[nodiscard]] bool in_scope(CoreScope s) const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return s.gen == conf_str_gen_;
    }

    [[nodiscard]] const FixedStr<64, StrFit::Clip>& core_name() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return core_name_;
    }

    [[nodiscard]] const PathText& saved_cfg_stem() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return saved_cfg_stem_;
    }

    [[nodiscard]] const RememberedStem& remembered_stem() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return remembered_stem_;
    }

    [[nodiscard]] const MraFacts& pending_facts() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return pending_facts_;
    }

    [[nodiscard]] bool facts_pending() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return facts_pending_;
    }

    [[nodiscard]] std::string defmra_elected(
        std::string_view text,
        const UiRequest::LoadCore& pending
    ) const;

    [[nodiscard]] std::string_view defmra_rel() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return defmra_rel_;
    }

    [[nodiscard]] bool generic_identity() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return generic_identity_;
    }

    [[nodiscard]] bool link_wide() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return link_wide_;
    }

    [[nodiscard]] bool core_live() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return core_live_;
    }

    [[nodiscard]] bool made_with_manifest() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return made_with_manifest_;
    }

    [[nodiscard]] infra::OptRef<const cores::CoreFactory> load_row() const;

    [[nodiscard]] const cores::CoreProfile& loaded_profile() const;
    [[nodiscard]] bool owns_loader() const;

    [[nodiscard]] const PathText& manifest_rel() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return manifest_rel_;
    }

    [[nodiscard]] std::string_view manifest_text() const noexcept;

    [[nodiscard]] const CheatCatalog& cheats() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return cheats_;
    }

    [[nodiscard]] const Tally& tally() const noexcept {
        TASTY_SEAT_BODY(NegotiatedCore);
        return tally_;
    }

private:
    void intern_manifest_(std::string_view rel);
    [[nodiscard]] std::vector<std::uint8_t> read_manifest_(std::string_view rel) const;
    void pack_manifest_cheats_(std::span<const std::uint8_t> doc);
    void elect_defmra_(std::string_view text, const UiRequest::LoadCore& pending);
    [[nodiscard]] proto::TxSlabId intern_saved_cfg_(std::string_view core_name);

    const LinkRxChannel<SeatTag::Main>& rx_;
    ConfStrCell& conf_str_cell_;
    const svc::Vfs& vfs_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    CheatCatalog cheats_{};

    FixedStr<64, StrFit::Clip> core_name_{};
    bool generic_identity_ = false;

    PathText saved_cfg_stem_{};

    RememberedStem remembered_stem_{};

    std::optional<proto::ConfStr> conf_str_{};
    proto::BindGeneration conf_str_gen_{};

    bool core_live_ = false;
    bool link_wide_ = false;

    cores::RamImageRecipe pending_ram_image_{};
    bool made_with_manifest_ = false;
    PathText manifest_rel_{};
    std::vector<std::uint8_t> manifest_doc_{};
    MraFacts pending_facts_{};
    bool facts_pending_ = false;
    std::string defmra_rel_;
    Tally tally_{};
};

}
