// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "app/load_request.h"
#include "app/mra_facts.h"
#include "app/ui_request.h"
#include "cores/manifest_doc_facts.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class PendingLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    PendingLoad() noexcept = default;

    void arm(const LoadRequest& req) noexcept;

    [[nodiscard]] const LoadRequest& request() const noexcept { return req_; }

    [[nodiscard]] std::string_view path() const noexcept { return req_.path.view(); }

private:
    LoadRequest req_{};
};

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
