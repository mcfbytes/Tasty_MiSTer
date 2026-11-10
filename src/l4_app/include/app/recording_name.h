// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"

namespace mister::app {

using RecPath = FixedStr<256, StrFit::Reject>;

using SegmentText = FixedStr<8, StrFit::Reject>;

struct SegmentIndex {
    std::uint16_t v{};

    [[nodiscard]] static constexpr SegmentIndex last() noexcept {
        return SegmentIndex{std::numeric_limits<std::uint16_t>::max()};
    }

    friend constexpr bool operator==(SegmentIndex, SegmentIndex) = default;

    [[nodiscard]] SegmentText text() const noexcept {
        char buf[SegmentText::kBufSize]{};
        const int n = std::snprintf(buf, sizeof buf, "_%03u", static_cast<unsigned>(v));
        SegmentText out{};
        if (n > 0 && static_cast<std::size_t>(n) < sizeof buf)
            (void)out.assign(std::string_view{buf, static_cast<std::size_t>(n)});
        return out;
    }
};

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
