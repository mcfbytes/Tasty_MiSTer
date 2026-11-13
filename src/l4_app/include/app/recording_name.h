// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/segment_index.h"
#include "infra/fixed_str.h"

namespace mister::app {

using RecPath = FixedStr<256, StrFit::Reject>;

class RecordingName {
public:
    static constexpr std::string_view kAviSuffix = ".avi";

    static constexpr std::string_view kSidecarSuffix = ".frames.tsv";

    [[nodiscard]] static std::optional<RecordingName> from_sidecar(
        std::string_view sidecar
    ) noexcept {
        RecordingName name;
        if (!name.sidecar_.assign(sidecar))
            return std::nullopt;
        std::string_view base = name.sidecar_.view();
        if (base.size() > kSidecarSuffix.size() && base.ends_with(kSidecarSuffix)) {
            base.remove_suffix(kSidecarSuffix.size());
        } else if (base.size() > kTsv.size() && base.ends_with(kTsv)) {
            base.remove_suffix(kTsv.size());
        }
        if (base.empty())
            return std::nullopt;
        (void)name.base_.assign(base);
        return name;
    }

    [[nodiscard]] RecPath sidecar() const noexcept { return sidecar_; }

    [[nodiscard]] RecPath segment(SegmentIndex no) const noexcept {
        const SegmentText text = no.text();
        if (text.empty())
            return RecPath{};
        RecPath out{};
        (void)out.assign(base_.view());
        if (!out.append(text.view()))
            return RecPath{};
        return out.append(kAviSuffix) ? out : RecPath{};
    }

    [[nodiscard]] bool fits() const noexcept { return !segment(SegmentIndex::last()).empty(); }

    [[nodiscard]] static bool names_sidecar(std::string_view path) noexcept {
        return path.size() > kTsv.size() && path.ends_with(kTsv);
    }

private:
    static constexpr std::string_view kTsv = ".tsv";

    RecordingName() = default;

    RecPath base_{};
    RecPath sidecar_{};
};

}
