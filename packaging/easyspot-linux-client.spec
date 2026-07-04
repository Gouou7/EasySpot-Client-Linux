Name:           easyspot-linux-client
Version:        0.1.0
Release:        1%{?dist}
Summary:        Native Fedora/KDE client for EasySpot Android hotspot control

License:        GPL-3.0-only
URL:            https://example.invalid/easyspot-linux-client
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  desktop-file-utils
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtconnectivity-devel

Requires:       bluez
Requires:       NetworkManager
Recommends:     kwallet

%description
EasySpot Linux Client is a native Qt 6 Widgets desktop companion for the
EasySpot Android hotspot service. It sends BLE hotspot control commands through
BlueZ DBus and connects Fedora/KDE to the configured hotspot through
NetworkManager DBus.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop
%ctest

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/16x16/apps/%{name}.png
%{_datadir}/icons/hicolor/24x24/apps/%{name}.png
%{_datadir}/icons/hicolor/32x32/apps/%{name}.png
%{_datadir}/icons/hicolor/48x48/apps/%{name}.png
%{_datadir}/icons/hicolor/64x64/apps/%{name}.png
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png

%changelog
* Sat Jul 04 2026 EasySpot Maintainers <noreply@example.invalid> - 0.1.0-1
- Initial Fedora package.
