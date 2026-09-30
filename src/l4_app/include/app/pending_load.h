// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "app/load_request.h"
#include "app/mra_facts.h"
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

    [[nodiscard]] Ex<std::string> resolve(const svc::Vfs& vfs) const;

    [[nodiscard]] MraFacts mra_facts(const svc::Vfs& vfs) const;

    [[nodiscard]] Ex<void> validate(const svc::Vfs& vfs) const;

private:
    [[nodiscard]] Ex<std::string_view> manifest_doc_(const svc::Vfs& vfs) const;
    [[nodiscard]] Ex<std::string> resolve_mra_(const svc::Vfs& vfs) const;

    LoadRequest req_{};
    mutable std::string doc_;
    mutable bool doc_read_ = false;
};

[[nodiscard]] MraFacts mra_facts_at(const svc::Vfs& vfs, std::string_view rel);

[[nodiscard]] Ex<std::string> resolve_bitstream(const svc::Vfs& vfs, std::string_view rel,
                                                XmlKind kind);

inline constexpr std::string_view kFrontEndImage = "menu.rbf";
[[nodiscard]] bool names_front_end_image(std::string_view rel, XmlKind kind) noexcept;

}  // namespace mister::app
