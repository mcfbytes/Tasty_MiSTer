// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "cores/manifest_act.h"
#include "cores/manifest_counts.h"
#include "infra/error.h"
#include "svc/file.h"

namespace mister::cores {

class IManifestStager {
public:
    virtual ~IManifestStager() = default;

    [[nodiscard]] virtual std::optional<ManifestAct> next() = 0;

    [[nodiscard]] virtual std::unique_ptr<svc::IFile> take_image() = 0;

    virtual void mark_staged() noexcept = 0;

    virtual void mark_torn() noexcept = 0;

    [[nodiscard]] virtual std::string_view error() const noexcept = 0;

    [[nodiscard]] virtual std::optional<Errc> refusal() const noexcept = 0;
    [[nodiscard]] virtual ManifestCounts counts() const noexcept = 0;

protected:
    IManifestStager() = default;
    IManifestStager(const IManifestStager&) = default;
    IManifestStager& operator=(const IManifestStager&) = default;
};

}  // namespace mister::cores
