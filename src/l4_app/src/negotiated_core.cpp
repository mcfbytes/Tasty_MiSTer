// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/negotiated_core.h"

#include <memory>

#include "app/cheat_catalog_pack.h"
#include "cores/cheat_records.h"
#include "cores/core_profile.h"
#include "cores/manifest_doc_role.h"
#include "cores/registry.h"
#include "infra/byte_view.h"
#include "svc/file.h"

namespace mister::app {

const cores::CoreFactory* NegotiatedCore::load_row() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto hint = made_with_manifest() ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    const auto row = cores::find_core(core_name().view(), hint);
    return row ? *row : nullptr;
}

const cores::CoreProfile& NegotiatedCore::loaded_profile() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto hint = made_with_manifest() ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    const auto row = cores::find_core(core_name().view(), hint);
    return row ? *(*row)->profile : cores::profile_for({});
}

bool NegotiatedCore::owns_loader() const {
    TASTY_SEAT_BODY(NegotiatedCore);
    const cores::CoreFactory* row = load_row();
    return row != nullptr && row->make_loader != nullptr;
}

std::vector<std::uint8_t> NegotiatedCore::read_manifest_(std::string_view rel) const {
    TASTY_SEAT_BODY(NegotiatedCore);
    std::vector<std::uint8_t> buf;
    if (!rel.empty()) {
        auto f = vfs_.open(rel, svc::OpenMode::ReadWhole);
        if (f) {
            auto sz = (*f)->size();
            const auto role = cores::manifest_doc_role();
            const std::uint64_t kMaxDoc = role ? role->doc_max() : 16ull * 1024u * 1024u;
            if (sz && sz->v != 0 && sz->v <= kMaxDoc) {
                buf.resize(static_cast<std::size_t>(sz->v));
                std::size_t off = 0;
                bool ok = true;
                while (off < buf.size()) {
                    auto n = (*f)->read_at(
                        off,
                        std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off))
                    );
                    if (!n || *n == 0) {
                        ok = false;
                        break;
                    }
                    off += *n;
                }
                if (!ok)
                    buf.clear();
            }
        }
    }
    return buf;
}

void NegotiatedCore::pack_manifest_cheats_(std::span<const std::uint8_t> doc) {
    TASTY_SEAT_BODY(NegotiatedCore);
    const auto role = cores::manifest_doc_role();
    if (!role) {
        cheats_ = CheatCatalog{};
        return;
    }
    const std::unique_ptr<cores::ICheatRecords> records = role->cheats_of(infra::as_text(doc));
    pack_cheat_catalog(*records, cheats_);
}

}
