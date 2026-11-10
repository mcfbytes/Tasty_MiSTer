// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/irq_pin.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <string_view>
#include <utility>

#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/hex_text.h"
#include "infra/parse_int.h"
#include "infra/unique_fd.h"
#include "os/path_buf.h"
#include "os/small_file.h"

namespace mister::hal {
namespace {

constexpr std::size_t kInterruptsCap = 32u * 1024u;
constexpr std::size_t kMaskCap = 4096;
constexpr std::size_t kPathCap = 256;

constexpr bool is_blank(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && (is_blank(s.back()) || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::string_view last_token(std::string_view field) noexcept {
    field = trim(field);
    std::size_t i = field.size();
    while (i > 0 && !is_blank(field[i - 1]))
        --i;
    return field.substr(i);
}

std::optional<os::KernelIrq> line_number(std::string_view line) noexcept {
    std::size_t i = 0;
    while (i < line.size() && is_blank(line[i]))
        ++i;
    const std::size_t colon = line.find(':', i);
    if (colon == std::string_view::npos || colon == i)
        return std::nullopt;

    const auto n = infra::parse_int<std::uint32_t>(line.substr(i, colon - i));
    if (!n)
        return std::nullopt;
    return os::KernelIrq{*n};
}

bool names_action(std::string_view tail, std::string_view action) noexcept {
    while (!tail.empty()) {
        const std::size_t comma = tail.find(',');
        const std::string_view field = tail.substr(0, comma);
        if (last_token(field) == action)
            return true;
        if (comma == std::string_view::npos)
            break;
        tail.remove_prefix(comma + 1);
    }
    return false;
}

std::optional<std::uint32_t> hex_group(std::string_view g) noexcept {

    if (g.empty() || g.size() > 8)
        return std::nullopt;
    return infra::parse_int<std::uint32_t>(g, 16);
}

struct IrqPath {
    char text[kPathCap];
    bool fits;
};

IrqPath irq_path(const char* root, os::KernelIrq irq, const char* leaf) noexcept {
    IrqPath p{};
    p.fits = os::format_path(std::span<char>(p.text), "%s/irq/%u/%s", root, irq.v, leaf);
    return p;
}

[[nodiscard]] Ex<os::CpuMask> read_mask(const char* path) noexcept {
    char buf[kMaskCap + 1];
    const auto got = os::read_small_file(path, std::span<char>(buf));
    if (!got)
        return std::unexpected(got.error());
    if (got->size() > kMaskCap)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const auto mask = parse_cpu_mask(infra::as_text(*got));
    if (!mask)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return *mask;
}

[[nodiscard]] Ex<void> write_mask(const char* path, os::CpuMask mask) noexcept {
    std::array<char, 16> text{};
    if (!os::format_path(std::span<char>(text), "%s\n", infra::HexText{mask.v}.c_str())) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const std::string_view line{text.data()};
    const int fd = ::open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    UniqueFd guard{fd};
    for (;;) {
        const ssize_t n = ::write(guard.get(), line.data(), line.size());
        if (std::cmp_equal(n, line.size()))
            return {};
        if (n < 0 && errno == EINTR)
            continue;
        const auto e = static_cast<std::uint32_t>(n < 0 ? errno : EIO);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
}

struct InterruptsList {
    std::string_view text;
    bool readable = false;
    bool truncated = false;
    int err = 0;
};

using InterruptsBuf = char[kInterruptsCap + 1];

InterruptsList read_interrupts(InterruptsBuf& buf, const char* proc_root) noexcept {
    InterruptsList l{};
    char path[kPathCap];
    if (!os::format_path(std::span<char>(path), "%s/interrupts", proc_root)) {
        l.err = ENAMETOOLONG;
        return l;
    }
    const auto got = os::read_small_file(path, std::span<char>(buf));
    if (!got) {
        l.err = static_cast<int>(got.error().detail);
        return l;
    }
    l.readable = true;
    l.truncated = got->size() > kInterruptsCap;
    l.text = infra::as_text(*got);
    if (l.truncated)
        l.text = l.text.substr(0, kInterruptsCap);
    return l;
}

IrqPinResult pin_resolved(
    const IrqPin& pin,
    const InterruptsList& list,
    const char* proc_root
) noexcept {
    IrqPinResult r{};
    r.action = pin.action;
    r.requested = pin.cpus;
    if (!list.readable) {
        r.outcome = IrqPinOutcome::NoInterruptsFile;
        r.err = list.err;
        return r;
    }
    const auto irq = irq_of_action(list.text, pin.action);
    if (!irq) {
        r.outcome = list.truncated ? IrqPinOutcome::ListTruncated : IrqPinOutcome::NoSuchAction;
        return r;
    }
    r.irq = *irq;

    const IrqPath smp = irq_path(proc_root, *irq, "smp_affinity");
    if (!smp.fits) {
        r.outcome = IrqPinOutcome::WriteRefused;
        r.err = ENAMETOOLONG;
        return r;
    }
    if (const auto w = write_mask(smp.text, pin.cpus); !w) {
        r.outcome = IrqPinOutcome::WriteRefused;
        r.err = static_cast<int>(w.error().detail);
        return r;
    }
    const auto back = read_mask(smp.text);
    if (!back) {
        r.outcome = IrqPinOutcome::ReadbackUnreadable;
        r.err = static_cast<int>(back.error().detail);
        return r;
    }
    r.read_back = *back;
    r.outcome = *back == pin.cpus ? IrqPinOutcome::Applied : IrqPinOutcome::ReadbackMismatch;

    const IrqPath eff_path = irq_path(proc_root, *irq, "effective_affinity");
    if (eff_path.fits) {
        if (const auto eff = read_mask(eff_path.text); eff) {
            r.effective = *eff;
            r.effective_known = true;
        }
    }
    return r;
}

}

std::optional<os::KernelIrq> irq_of_action(
    std::string_view interrupts,
    std::string_view action
) noexcept {
    if (action.empty())
        return std::nullopt;
    while (!interrupts.empty()) {
        const std::size_t nl = interrupts.find('\n');
        const std::string_view line = interrupts.substr(0, nl);
        if (const auto irq = line_number(line); irq) {
            if (names_action(line.substr(line.find(':') + 1), action))
                return irq;
        }
        if (nl == std::string_view::npos)
            break;
        interrupts.remove_prefix(nl + 1);
    }
    return std::nullopt;
}

std::optional<os::CpuMask> parse_cpu_mask(std::string_view text) noexcept {
    text = trim(text);
    if (text.empty())
        return std::nullopt;

    std::uint32_t low = 0;
    for (;;) {
        const std::size_t comma = text.find(',');
        const auto g = hex_group(text.substr(0, comma));
        if (!g)
            return std::nullopt;
        if (comma == std::string_view::npos) {
            low = *g;
            break;
        }
        if (*g != 0)
            return std::nullopt;
        text.remove_prefix(comma + 1);
    }
    return os::CpuMask{low};
}

IrqPinResult pin_irq_affinity(const IrqPin& pin, const char* proc_root) noexcept {
    InterruptsBuf buf;
    return pin_resolved(pin, read_interrupts(buf, proc_root), proc_root);
}

IrqPinReport pin_irq_affinities(
    std::span<const IrqPin> pins,
    const ThreadMap& map,
    const char* proc_root
) noexcept {
    IrqPinReport rep{};
    rep.declared = pins.size();
    if (!irq_pins_well_formed(pins, map)) {
        rep.table_refused = true;
        rep.first_err = E2BIG;
        return rep;
    }
    rep.count = pins.size();
    rep.all_applied = true;
    if (rep.count == 0)
        return rep;
    InterruptsBuf buf;
    const InterruptsList list = read_interrupts(buf, proc_root);
    for (std::size_t i = 0; i < rep.count; ++i) {
        rep.pins[i] = pin_resolved(pins[i], list, proc_root);
        if (rep.pins[i].outcome != IrqPinOutcome::Applied)
            rep.all_applied = false;
        if (rep.first_err == 0)
            rep.first_err = rep.pins[i].err;
    }
    return rep;
}

}
