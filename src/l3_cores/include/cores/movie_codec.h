// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/digest_value.h"
#include "cores/ram_image.h"
#include "infra/error.h"
#include "proto/status_word.h"
#include "svc/file.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct MovieArchiveFormat;

class IMovieCodec {
public:
    static constexpr std::size_t kPorts = 4;

    static constexpr std::size_t kLineMax = 256;

    enum class Refusal : std::uint8_t {
        None,
        NotAMovie,
        Savestate,
        Binary,
        Disk,
        PortType,
        Multitap,
        Checksum,
        Command,
        TooLong,
        BadLine,
        Setting,
        Archive,
        Subframe,
        System,
        ChdUnsupported,
        kCount,
    };
    enum class Region : std::uint8_t { Ntsc, Pal };

    static constexpr std::size_t kColumnsMax = 48;

    struct Facts {
        std::uint16_t version = 0;
        Region region = Region::Ntsc;
        std::uint8_t ports = 0;
        bool has_digest = false;

        std::uint16_t layout = 0;
        std::uint8_t columns_n = 0;
        std::array<std::uint8_t, kColumnsMax> columns{};
        DigestValue digest{};

        bool has_firmware = false;
        DigestValue firmware{};
        bool has_rerecords = false;
        std::uint32_t rerecords = 0;
    };
    struct Frame {
        std::uint8_t commands = 0;
        std::array<std::uint32_t, kPorts> mask{};
    };

    struct Raster {
        std::uint32_t period_ns = 0;
        std::uint32_t line0_ns = 0;
    };

    enum class Parity : std::uint8_t { Any, Even, Odd, AfterSilence };

    enum class PowerOnEvent : std::uint8_t { LoadEnd, ResetPulse };

    struct PowerOn {
        std::uint64_t clocks = 0;
        std::uint32_t clock_hz = 0;

        std::uint8_t lo = 0;
        std::uint8_t width = 0;
        std::uint16_t when = 0;
        Parity p0 = Parity::Any;
        PowerOnEvent event = PowerOnEvent::LoadEnd;

        std::uint8_t late_frames = 0;
    };

    struct LeadRange {
        std::int32_t min = -8;
        std::int32_t max = 64;
    };

    struct SettingVerdict {
        bool ok = true;
        std::string_view setting{};
    };

    struct SettingNeed {
        std::uint8_t lo = 0;
        std::uint8_t width = 0;
        std::uint16_t allowed = 0;
        std::uint8_t preferred = 0;
        std::string_view name{};
        [[nodiscard]] unsigned value_in(const proto::StatusWord& live) const noexcept;
        [[nodiscard]] bool met_by(const proto::StatusWord& live) const noexcept;
        [[nodiscard]] bool settable() const noexcept { return ((allowed >> preferred) & 1u) != 0; }
    };
    static constexpr std::size_t kNeedsMax = 32;

    struct SettingNeeds {
        std::array<SettingNeed, kNeedsMax> rows{};
        std::size_t n = 0;
        void add(const SettingNeed& need) noexcept {
            if (n < rows.size()) rows[n++] = need;
        }
        [[nodiscard]] std::span<const SettingNeed> view() const& noexcept {
            return {rows.data(), n};
        }
        std::span<const SettingNeed> view() const&& = delete;
    };

    virtual ~IMovieCodec() = default;

    [[nodiscard]] virtual const MovieArchiveFormat* archive() const noexcept { return nullptr; }

    [[nodiscard]] virtual Ex<std::unique_ptr<svc::IFile>> open_movie(const svc::Vfs& vfs,
                                                                     std::string_view path) const {
        return open_plain_or_archive(archive(), vfs, path);
    }

    [[nodiscard]] virtual bool starts_log(std::string_view line) const noexcept = 0;

    [[nodiscard]] virtual bool ends_log(std::string_view line) const noexcept {
        (void)line;
        return false;
    }
    [[nodiscard]] virtual Ex<Facts> header_line(std::string_view line,
                                                const Facts& so_far) const noexcept = 0;

    [[nodiscard]] virtual Ex<void> finish_header(const Facts& f) const noexcept = 0;
    [[nodiscard]] virtual Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept = 0;

    [[nodiscard]] virtual SettingNeeds setting_needs(const Facts& f) const noexcept = 0;

    enum class RamFill : std::uint8_t { Zero, Ff, Random };

    [[nodiscard]] virtual std::optional<SettingNeed> ram_fill_need(RamFill fill) const noexcept {
        (void)fill;
        return std::nullopt;
    }

    [[nodiscard]] virtual RamImageRecipe power_on_ram(const Facts& f) const noexcept {
        (void)f;
        return {};
    }

    [[nodiscard]] std::optional<SettingNeeds> setting_needs_for(
        const Facts& f, std::optional<RamFill> fill) const noexcept;
    [[nodiscard]] SettingVerdict check_settings(const proto::StatusWord& live,
                                                const Facts& f) const noexcept;
    [[nodiscard]] virtual Raster raster(const Facts& f) const noexcept = 0;
    [[nodiscard]] virtual PowerOn power_on(const Facts& f) const noexcept = 0;

    [[nodiscard]] virtual std::int32_t default_lead(const Facts& f) const noexcept = 0;
    [[nodiscard]] virtual LeadRange lead_range() const noexcept { return {}; }

    [[nodiscard]] std::optional<std::uint32_t> power_on_ns(const proto::StatusWord& live,
                                                           const Facts& f) const noexcept;

    struct DigestSpan {
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
    };
    static constexpr std::size_t kRomHead = 16;

    [[nodiscard]] virtual std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept = 0;

    [[nodiscard]] virtual bool rom_matches(const DigestValue& d, const Facts& f) const noexcept {
        return f.has_digest && d == f.digest;
    }

    [[nodiscard]] virtual std::string_view games_folder() const noexcept { return {}; }

    [[nodiscard]] virtual std::uint8_t rom_digit() const noexcept = 0;

    static constexpr std::uint64_t kRomMax = 4u << 20;

    struct DigestSource {
        std::string file{};
        DigestSpan span{};
        std::vector<std::uint8_t> prefix{};

        std::optional<std::vector<std::uint8_t>> alt_prefix{};
    };

    [[nodiscard]] virtual Ex<DigestSource> digest_source(const svc::Vfs& vfs,
                                                         std::string_view rom) const {
        return head_digest_source(vfs, rom);
    }

    [[nodiscard]] virtual std::string_view disc_images() const noexcept { return {}; }

    struct Companion {
        std::array<std::string, 2> paths{};
        std::uint64_t size = 0;
    };
    [[nodiscard]] virtual std::optional<Companion> companion(std::string_view rom,
                                                             const Facts& f) const {
        (void)rom;
        (void)f;
        return std::nullopt;
    }

protected:
    [[nodiscard]] static unsigned status_field(const proto::StatusWord& s, unsigned lo,
                                               unsigned width) noexcept;

    [[nodiscard]] Ex<DigestSource> head_digest_source(const svc::Vfs& vfs,
                                                      std::string_view rom) const;

    [[nodiscard]] static Ex<std::unique_ptr<svc::IFile>> open_plain_or_archive(
        const MovieArchiveFormat* container, const svc::Vfs& vfs, std::string_view path);

    IMovieCodec() = default;
    IMovieCodec(const IMovieCodec&) = default;
    IMovieCodec& operator=(const IMovieCodec&) = default;
};

void note_rerecords(std::string_view key, std::string_view val, IMovieCodec::Facts& f) noexcept;

[[nodiscard]] const IMovieCodec* movie_codec_for(std::string_view conf_str_name,
                                                 std::string_view movie_path) noexcept;

[[nodiscard]] const IMovieCodec* movie_codec_for_path(std::string_view movie_path) noexcept;

}  // namespace mister::cores
