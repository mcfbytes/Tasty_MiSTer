// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra/arcade_facts.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include "infra/byte_view.h"
#include "infra/prelude.h"
#include "svc/card_dirs.h"
#include "svc/read_bounded.h"

namespace mister::cores::mra {

namespace {

mra::DipWord saved_dips(const svc::Vfs& vfs, std::string_view name, mra::DipWord def) {
    auto f = vfs.open(svc::card_rel({svc::kConfigDir, "dips", name}), svc::OpenMode::Read);
    if (!f)
        return def;
    mra::DipWord::Bytes raw{};
    auto r = (*f)->read_at(proto::kFileStart, std::as_writable_bytes(std::span<std::uint8_t>(raw)));
    if (!r || *r != infra::ByteCount{raw.size()})
        return def;
    return mra::DipWord::from_le_bytes(raw);
}

}

[[nodiscard]] Ex<std::string> manifest_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
) {
    if (path.empty())
        return std::string{};
    if (!text.empty()) {
        if (text.size() > mra::kMraDocMax) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        return std::string(text);
    }
    auto f = vfs.open(path, svc::OpenMode::Read);
    if (!f)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    const auto sz = (*f)->size();
    if (!sz || !sz->within(svc::FileSize{1u}, svc::FileSize{mra::kMraDocMax})) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    auto bytes = svc::read_bounded(**f, mra::kMraDocMax);
    if (!bytes)
        return std::unexpected(bytes.error());
    return std::string(infra::as_text(std::span(*bytes)));
}

[[nodiscard]] Ex<ArcadeDocument> read_arcade_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
) {
    ArcadeDocument out;
    if (path.empty())
        return out;
    ArcadeFacts& f = out.facts;
    f.dip_name = mra::sibling_name(path, ".dip");
    f.nvm_name = mra::sibling_name(path, ".nvm");
    auto read = manifest_document(text, path, vfs);
    if (!read)
        return std::unexpected(read.error());
    const std::string owned = std::move(*read);
    text = owned;
    mra::MraDoc doc;
    doc.parse(text);
    out.roms = std::move(doc.roms);
    out.nvrams = std::move(doc.nvrams);
    f.nvram = doc.nvram_save();
    f.switches = std::move(doc.switches);
    f.dip_cur = saved_dips(vfs, f.dip_name, f.switches.dip_def);
    return out;
}

}
