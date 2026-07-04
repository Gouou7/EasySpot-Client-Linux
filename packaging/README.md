# Fedora RPM Packaging

This directory contains a Fedora-oriented RPM spec for EasySpot Linux Client.

The package is intended for Fedora 44 and follows the current project layout:

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
