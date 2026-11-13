// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/usb_adapter_cycle.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "infra/unique_fd.h"
#include "os/write_all.h"

namespace mister::os {
namespace {

constexpr std::string_view kBluetoothDir{"bluetooth"};
constexpr std::string_view kAdapterPrefix{"hci"};
constexpr std::string_view kAuthorized{"authorized"};
constexpr std::string_view kDeauthorize{"0"};
constexpr std::string_view kAuthorize{"1"};

[[nodiscard]] bool dot_entry(std::string_view name) noexcept {
    return name == "." || name == "..";
}

[[nodiscard]] bool has_adapter(const std::string& dir) noexcept {
    ::DIR* device = ::opendir(dir.c_str());
    if (device == nullptr)
        return false;
    bool found = false;
    while (const ::dirent* iface = ::readdir(device)) {
        const std::string_view name{iface->d_name};
        if (dot_entry(name))
            continue;
        std::string bt = dir;
        bt.append("/").append(name).append("/").append(kBluetoothDir);
        ::DIR* radio = ::opendir(bt.c_str());
        if (radio == nullptr)
            continue;
        while (const ::dirent* hci = ::readdir(radio)) {
            if (std::string_view{hci->d_name}.starts_with(kAdapterPrefix))
                found = true;
        }
        ::closedir(radio);
        if (found)
            break;
    }
    ::closedir(device);
    return found;
}

[[nodiscard]] Ex<void> write_text(const std::string& path, std::string_view text) noexcept {
    UniqueFd fd{::open(path.c_str(), O_WRONLY | O_CLOEXEC)};
    if (!fd.valid())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return write_all(fd, std::as_bytes(std::span{text.data(), text.size()}));
}

}

Ex<UsbDeviceCount> UsbAdapterCycle::cycle() const noexcept {
    ::DIR* root = ::opendir(root_.c_str());
    if (root == nullptr) {
        if (errno == ENOENT)
            return UsbDeviceCount{};
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    UsbDeviceCount cycled{};
    while (const ::dirent* entry = ::readdir(root)) {
        const std::string_view name{entry->d_name};
        if (dot_entry(name))
            continue;
        std::string device = root_;
        device.append("/").append(name);
        if (!has_adapter(device))
            continue;
        const std::string toggle = device + "/" + std::string{kAuthorized};
        if (!write_text(toggle, kDeauthorize))
            continue;
        (void)write_text(toggle, kAuthorize);
        cycled = cycled.plus_one();
    }
    ::closedir(root);
    return cycled;
}

}
