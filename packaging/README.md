# Fedora RPM Packaging

This directory contains a Fedora-oriented RPM spec for EasySpot Linux Client.

The spec follows the current project layout:

- Qt 6 Widgets application built with CMake.
- BLE through BlueZ DBus.
- Wi-Fi through NetworkManager DBus.
- Hotspot password storage through Freedesktop Secret Service/KWallet.
- XDG desktop entry and hicolor icon installation.

## Dependency Policy

The spec intentionally avoids strict version pins so Fedora package upgrades are not blocked unnecessarily.

Explicit runtime dependencies are limited to services the app directly needs:

- `bluez`
- `NetworkManager`

`kwallet` is a weak dependency through `Recommends` because any Freedesktop Secret Service provider can satisfy password storage at runtime.

Qt runtime library dependencies are expected to be generated automatically by RPM from the linked binary.

That automatic dependency generation is also why one binary RPM is not a good compatibility target for multiple Fedora releases. A Qt/C++ RPM built on a newer Fedora records runtime requirements for the Qt, glibc, libstdc++, and other library symbols found on that build system. Older Fedora releases may not provide those exact symbols, even when the application source would compile and run there.

For broad Fedora compatibility, publish one source RPM and rebuild it in each target Fedora chroot. Building on the oldest target can sometimes produce a binary that installs on newer releases, but the reliable RPM-native answer is still per-release binary RPMs.

## Build Dependencies

Install packaging tools and development dependencies:

```sh
sudo dnf install rpm-build rpmdevtools cmake gcc-c++ desktop-file-utils qt6-qtbase-devel qt6-qtconnectivity-devel
```

## Build an RPM

Create the RPM build tree:

```sh
rpmdev-setuptree
```

Create a source archive from the repository root. The archive must contain a top-level directory named `easyspot-linux-client-0.1.0`:

```sh
./packaging/make-source-tarball.sh
```

The script writes the archive to `~/rpmbuild/SOURCES/` by default and excludes local Agent context, Git metadata, and CMake build directories.

If you prefer a committed-tree-only archive, use `git archive` after committing the release contents:

```sh
git archive --format=tar.gz --prefix=easyspot-linux-client-0.1.0/ -o ~/rpmbuild/SOURCES/easyspot-linux-client-0.1.0.tar.gz HEAD
```

Build the binary RPM and source RPM:

```sh
rpmbuild -ba packaging/easyspot-linux-client.spec
```

The results are written under:

```text
~/rpmbuild/RPMS/
~/rpmbuild/SRPMS/
```

## Notes

- Replace the `URL` and maintainer email in the spec before publishing.
- Keep `License:` synchronized with the project `LICENSE` file.
- The package does not set Linux capabilities on the installed binary. The current BlueZ DBus backend is designed to avoid the earlier QtBluetooth address-type capability issue.

## Multi-Fedora Builds

Use `mock` or a build service such as COPR/OBS to build the same SRPM once per target Fedora release. This keeps the spec package-name based while letting each Fedora release resolve the correct Qt and system library ABI requirements.

Install the local tools:

```sh
sudo dnf install rpm-build rpmdevtools mock
sudo usermod -aG mock "$USER"
```

Log out and back in after joining the `mock` group.

Build for the default Fedora chroots listed in the helper:

```sh
./packaging/build-fedora-rpms.sh
```

Or choose explicit targets:

```sh
./packaging/build-fedora-rpms.sh packaging/artifacts fedora-42-x86_64 fedora-43-x86_64 fedora-44-x86_64
```

The results are written under `packaging/artifacts/<chroot>/`.

For public distribution, prefer COPR or OBS over uploading one locally built binary RPM. They rebuild from the SRPM in clean target chroots and produce separate repositories per Fedora release.
