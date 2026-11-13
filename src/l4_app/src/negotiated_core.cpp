// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/negotiated_core.h"

#include <memory>

#include "app/cheat_catalog_pack.h"
#include "app/config_rel.h"
#include "app/stream_io.h"
#include "cores/cheat_records.h"
#include "cores/core_profile.h"
#include "cores/manifest_doc_role.h"
#include "cores/registry.h"
#include "infra/byte_view.h"

namespace mister::app {

proto::TxSlabId NegotiatedCore::key_names(
    const proto::ConfStr& cs,
    const UiRequest::LoadCore& pending
) {
    TASTY_SEAT_BODY(NegotiatedCore);
    const std::string_view name = cs.name();

    remembered_stem_ = RememberedStem::of(name, pending_facts_);
    elect_defmra_(cs.default_manifest(), pending);
    const proto::TxSlabId cfg = intern_saved_cfg_(effective_name(name, pending_facts_));

    (void)saved_cfg_stem_.assign(effective_name(name, pending_facts_));
    return cfg;
}

void NegotiatedCore::latch_mra_facts(const UiRequest::LoadCore& pending) {
    TASTY_SEAT_BODY(NegotiatedCore);
    facts_pending_ = false;
    pending_facts_ = MraFacts{};
    defmra_rel_.clear();
    if (pending.xml != XmlKind::Mra || pending.path.empty()) {
        intern_manifest_({});
        return;
    }
    pending_facts_ = mra_facts_at(vfs_, pending.path.view());
    facts_pending_ = true;
    intern_manifest_(pending.path.view());
}

void NegotiatedCore::forget() noexcept {
    TASTY_SEAT_BODY(NegotiatedCore);

    conf_str_.reset();
    pending_ram_image_ = {};
    core_live_ = false;
}

void NegotiatedCore::note_build(std::string_view built_from) {
    TASTY_SEAT_BODY(NegotiatedCore);
    made_with_manifest_ = !built_from.empty();
    if (!made_with_manifest_ || !manifest_rel_.assign(built_from))
        manifest_rel_ = PathText{};
}

std::string NegotiatedCore::defmra_elected(
    std::string_view text,
    const UiRequest::LoadCore& pending
) const {
    TASTY_SEAT_BODY(NegotiatedCore);
    if (pending.xml == XmlKind::Mra)
        return {};
    std::string rel = default_manifest_rel(text);
    if (rel.empty() || !vfs_.file_exists(rel))
        return {};
    return rel;
}

std::string_view NegotiatedCore::manifest_text() const noexcept {
    TASTY_SEAT_BODY(NegotiatedCore);
    if (manifest_doc_.empty())
        return {};
    return infra::as_text(std::span(manifest_doc_));
}

infra::OptRef<const cores::CoreFactory> NegotiatedCore::load_row() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto hint = made_with_manifest() ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    return cores::find_core(core_name().view(), hint);
}

const cores::CoreProfile& NegotiatedCore::loaded_profile() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto hint = made_with_manifest() ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    return cores::profile_for(core_name().view(), hint);
}

bool NegotiatedCore::owns_loader() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto row = load_row();
    return row && row->make_loader != nullptr;
}

void NegotiatedCore::intern_manifest_(std::string_view rel) {
    manifest_doc_ = read_manifest_(rel);
    pack_manifest_cheats_(manifest_doc_);
}

std::vector<std::uint8_t> NegotiatedCore::read_manifest_(std::string_view rel) const {
    TASTY_SEAT_BODY(NegotiatedCore);
    if (rel.empty())
        return {};
    const auto role = cores::manifest_doc_role();
    const proto::FileSize max_doc = role ? proto::FileSize{role->doc_max()} : kManifestDocMax;

    auto doc = read_file(vfs_, rel, max_doc);
    return doc ? std::move(*doc) : std::vector<std::uint8_t>{};
}

void NegotiatedCore::pack_manifest_cheats_(std::span<const std::uint8_t> doc) {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto role = cores::manifest_doc_role();
    if (!role) {
        cheats_ = CheatCatalog{};
        return;
    }
    const std::unique_ptr<cores::ICheatRecords> records = role->cheats_of(infra::as_text(doc));
    cheats_ = pack_cheat_catalog(*records);
}

void NegotiatedCore::elect_defmra_(std::string_view text, const UiRequest::LoadCore& pending) {
    defmra_rel_.clear();

    if (pending.xml == XmlKind::Mra)
        return;
    pending_facts_ = MraFacts{};
    intern_manifest_({});
    const std::string rel = defmra_elected(text, pending);
    if (rel.empty())
        return;
    pending_facts_ = mra_facts_at(vfs_, rel);
    defmra_rel_ = rel;
    intern_manifest_(rel);
}

proto::TxSlabId NegotiatedCore::intern_saved_cfg_(std::string_view core_name) {
    const auto rel = core_cfg_rel(core_name);
    if (!rel)
        return {};
    return intern_exact(vfs_, inbox_, rel->view(), sizeof(proto::StatusWord::Bytes));
}

}
