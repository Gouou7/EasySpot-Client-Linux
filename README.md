# EasySpot Linux Client

EasySpot Linux Client is a native Fedora/KDE desktop companion for the EasySpot Android hotspot service. It sends hotspot on/off commands to the Android service over BLE, then asks NetworkManager to connect Linux to the configured hotspot Wi-Fi network.

The application uses standard Qt 6 Widgets and Qt Designer `.ui` files. It intentionally avoids custom stylesheets, custom painting, and hard-coded colors so KDE Breeze, the active Qt platform theme, fonts, icons, and light/dark mode remain in control.

## Features

- Turn the EasySpot Android hotspot on or off over BLE.
- Connect to the configured hotspot without sending a BLE command.
- Use BlueZ DBus for BLE discovery, GATT connection, and command writes.
- Use NetworkManager DBus to connect to Wi-Fi without invoking `nmcli`.
- Store the hotspot password in Freedesktop Secret Service, normally backed by KWallet on KDE.
- Store non-secret settings, such as SSID and preferences, with `QSettings`.
- Use a volatile NetworkManager Wi-Fi profile during connection activation so NetworkManager does not persist the hotspot password.
- Provide a KDE-friendly Qt Widgets settings window, system tray menu, desktop notifications, launch-at-login support, and debug summary copy.
- Monitor Wi-Fi state through NetworkManager DBus signals instead of constant background polling.

## Requirements

Target environment:

- Fedora with KDE Plasma
- BlueZ
- NetworkManager
- KWallet or another Freedesktop Secret Service provider
- Qt 6 development packages

Install common Fedora dependencies:

```sh
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtconnectivity-devel qt6-qttools-devel bluez NetworkManager kwallet
```

`qt6-qtbase-private-devel` is not required.

## Build and Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the app:

```sh
./build/easyspot-linux-client
```

Start directly in the system tray:

```sh
./build/easyspot-linux-client --tray
```

## Fedora RPM Packaging

Fedora RPM packaging files are in `packaging/`.

See `packaging/README.md` for the full local RPM build workflow. The spec targets Fedora 44 without strict dependency version pins, so normal Fedora package upgrades are not blocked.

## Usage

1. Start the EasySpot service on the Android phone.
2. Pair or allow the phone in KDE Bluetooth settings if the Android service requires encrypted BLE access.
3. Open EasySpot Linux Client.
4. Save the phone hotspot SSID and password.
5. Choose one of the actions:
   - `Turn Hotspot ON`: sends BLE `0x01`, waits briefly for the Android hotspot to appear, then connects Linux to the saved SSID.
   - `Connect to Hotspot`: connects Linux to the saved SSID without sending any BLE command.
   - `Turn Hotspot OFF`: sends BLE `0x00`, even if Linux is not connected to the hotspot.

When Linux is already connected to the configured SSID, `Turn Hotspot ON` and `Connect to Hotspot` are disabled in both the window and tray menu.

## KDE Notes

- Closing the main window hides it; EasySpot keeps running in the system tray until `Quit` is selected.
- Launch-at-login writes `~/.config/autostart/easyspot-linux-client.desktop` and starts EasySpot with `--tray`.
- Password saving fails visibly if no Secret Service provider is available. The app does not fall back to plaintext password storage.
- NetworkManager may show system authorization prompts depending on local policy.
- Older builds may have created persistent NetworkManager profiles named `EasySpot <SSID>`. This client does not remove those automatically; delete old profiles from KDE network settings if you want to clean up old saved Wi-Fi secrets.

## Debugging

Enable debug information in the settings window to copy BLE and workflow state for troubleshooting. BLE log lines are prefixed with `easyspot.ble:` and Wi-Fi log lines with `easyspot.wifi:`.

The BLE backend uses BlueZ DBus directly. If BLE writes fail, verify that BlueZ is running, the Android service is advertising, and the phone is paired if encrypted access is enabled.
