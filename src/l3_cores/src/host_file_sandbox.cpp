// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/host_file_sandbox.h"

#include "infra/prelude.h"
#include "svc/card_writer.h"
#include "svc/confine_under.h"

namespace mister::cores {

Ex<std::string> HostFileSandbox::resolve(std::string_view guest_path) const {
    return svc::ConfineRoot{root_}.confine(*vfs_, guest_path);
}

Ex<std::vector<svc::DirEntry>> HostFileSandbox::list() const {
    return vfs_->list_names(root_);
}

Ex<std::unique_ptr<svc::IFile>> HostFileSandbox::open(std::string_view guest_path) const {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    return vfs_->open_named(*host, svc::OpenMode::Read);
}

Ex<std::unique_ptr<svc::IFile>> HostFileSandbox::create(std::string_view guest_path) {
    return open(guest_path, svc::OpenMode::Truncate);
}

HostFileSandbox::Kind HostFileSandbox::kind(std::string_view guest_path) const {
    const auto host = resolve(guest_path);
    if (!host)
        return Kind::Absent;
    if (vfs_->dir_exists(*host))
        return Kind::Dir;
    if (vfs_->file_exists(*host))
        return Kind::File;
    return Kind::Absent;
}

Ex<std::vector<svc::DirEntry>> HostFileSandbox::list(std::string_view guest_dir) const {
    const auto host = resolve(guest_dir);
    if (!host)
        return std::unexpected(host.error());
    return vfs_->list_names(*host);
}

Ex<std::chrono::sys_seconds> HostFileSandbox::modified(std::string_view guest_path) const {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    return vfs_->modified(*host);
}

Ex<std::unique_ptr<svc::IFile>> HostFileSandbox::open(
    std::string_view guest_path,
    svc::OpenMode mode
) {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    if (svc::opens_read_only(mode))
        return vfs_->open_named(*host, mode);
    svc::CardWriter card{*vfs_};
    return card.open_named(*host, mode);
}

Ex<void> HostFileSandbox::remove(std::string_view guest_path) {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    svc::CardWriter card{*vfs_};
    return card.remove(*host);
}

Ex<void> HostFileSandbox::remove_dir(std::string_view guest_path) {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    svc::CardWriter card{*vfs_};
    return card.remove_dir(*host);
}

Ex<void> HostFileSandbox::make_dir(std::string_view guest_path) {
    const auto host = resolve(guest_path);
    if (!host)
        return std::unexpected(host.error());
    svc::CardWriter card{*vfs_};
    return card.ensure_dir(*host);
}

Ex<void> HostFileSandbox::move(const svc::MoveNames& guest_names) {
    const auto from = resolve(guest_names.from);
    if (!from)
        return std::unexpected(from.error());
    const auto to = resolve(guest_names.to);
    if (!to)
        return std::unexpected(to.error());
    svc::CardWriter card{*vfs_};
    return card.move(svc::MoveNames{.from = *from, .to = *to});
}

Ex<svc::Vfs::Space> HostFileSandbox::space() const {
    return vfs_->space(root_);
}

Ex<void> HostFileSandbox::make_root() {
    if (vfs_->dir_exists(root_))
        return {};
    svc::CardWriter card{*vfs_};
    return card.ensure_dir(root_);
}

}
