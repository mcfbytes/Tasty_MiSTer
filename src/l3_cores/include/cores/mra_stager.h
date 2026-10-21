// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/arcade_facts.h"
#include "cores/fill_run.h"
#include "cores/manifest_stager.h"
#include "cores/rom_bytes.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::mra {

class MraStager final : public IManifestStager {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::array<std::string_view, 15> kAudited{
        "cave",
        "cavebanpresto",
        "fuuki",
        "guardians",
        "igspgm",
        "iremm90",
        "iremm92",
        "iremm107",
        "leland",
        "mcr3",
        "primalrage",
        "psikyo",
        "psikyosh2",
        "taitof2",
        "vball"
    };

    static constexpr std::uint32_t kPlainFillMax = 16u * 1024u * 1024u;

    [[nodiscard]] static bool audited(std::string_view rbf) noexcept;

    [[nodiscard]] static std::unique_ptr<IManifestStager> of(
        std::string_view doc,
        std::string_view manifest_rel,
        const svc::Vfs& vfs
    );

    MraStager(
        std::string_view doc,
        std::string_view manifest_rel,
        std::string_view mame_root,
        const svc::Vfs& vfs
    );
    ~MraStager() override;
    MraStager(const MraStager&) = delete;
    MraStager& operator=(const MraStager&) = delete;

    [[nodiscard]] std::optional<ManifestAct> next() override;
    [[nodiscard]] std::unique_ptr<svc::IFile> take_image() override;
    void mark_staged() noexcept override;
    void mark_torn() noexcept override;

    [[nodiscard]] std::string_view error() const noexcept override {
        TASTY_SEAT_BODY(MraStager);
        return error_;
    }

    [[nodiscard]] std::optional<Errc> refusal() const noexcept override {
        TASTY_SEAT_BODY(MraStager);
        return refused_;
    }

    [[nodiscard]] ManifestCounts counts() const noexcept override {
        TASTY_SEAT_BODY(MraStager);
        return counts_;
    }

private:
    enum class Leg : std::uint8_t { Roms, Dips, Done };

    struct Window {
        std::uint8_t index = 0;
        std::uint32_t bytes = 0;
        bool staged = false;
        bool torn = false;
    };

    [[nodiscard]] std::optional<ManifestAct> rom_();
    [[nodiscard]] std::optional<ManifestAct> nvram_(const NvramDecl& d);
    void finish_() noexcept;

    struct Assembly;
    std::unique_ptr<Assembly> a_;
    std::vector<NvramDecl> nvrams_;
    std::size_t nvram_next_ = 0;
    const svc::Vfs& vfs_;
    ArcadeFacts facts_;
    bool audited_ = false;
    Leg leg_ = Leg::Roms;
    std::optional<Window> window_;
    RomBytes image_;
    std::vector<FillRun> image_runs_;
    std::string error_;
    std::optional<Errc> refused_;
    std::uint32_t fill_refusals_ = 0;
    ManifestCounts counts_{};
};

}
