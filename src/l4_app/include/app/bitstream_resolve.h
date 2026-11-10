// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "app/mra_facts.h"
#include "app/ui_request.h"
#include "app/xml_kind.h"
#include "cores/manifest_doc_facts.h"
#include "infra/error.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

[[nodiscard]] MraFacts mra_facts_at(const svc::Vfs& vfs, std::string_view rel);

[[nodiscard]] cores::ManifestDocFacts build_doc_facts(
    const svc::Vfs& vfs,
    std::string_view text,
    std::string_view path
);

[[nodiscard]] Ex<std::string> resolve_bitstream(
    const svc::Vfs& vfs,
    std::string_view rel,
    XmlKind kind
);

[[nodiscard]] bool validate_load(const svc::Vfs& vfs, const UiRequest::LoadCore& req);

inline constexpr std::string_view kFrontEndImage = "menu.rbf";
[[nodiscard]] bool names_front_end_image(std::string_view rel, XmlKind kind) noexcept;

[[nodiscard]] bool same_image_name(std::string_view rel, std::string_view image) noexcept;

}
