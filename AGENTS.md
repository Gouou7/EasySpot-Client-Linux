# AGENTS.md

## Project Overview

- Native Fedora/KDE client for controlling an EasySpot Android hotspot.
- Main workflow: send an EasySpot BLE command to the Android service, then optionally connect Linux to the configured Wi-Fi hotspot.
- UI stack: C++20, Qt 6 Widgets, Qt Designer `.ui` files, CMake with `AUTOMOC`, `AUTOUIC`, and `AUTORCC`.
- System integration stack: BlueZ DBus for BLE, NetworkManager DBus for Wi-Fi, Freedesktop Secret Service/KWallet for hotspot password storage, XDG autostart for launch at login.
- Keep work inside this repository. Treat sibling Android, Python, and Windows client projects as reference-only unless the user explicitly asks to edit them.

## Project Structure

- `src/main.cpp`: application startup, icon resource initialization, object wiring, `--tray` handling.
- `src/MainWindow.*` and `ui/MainWindow.ui`: Qt Widgets settings and control window.
- `src/CredentialsDialog.*` and `ui/CredentialsDialog.ui`: SSID/password edit dialog.
- `src/TrayController.*`: `QSystemTrayIcon` menu, notifications, and tray visibility behavior.
- `src/HotspotWorkflowController.*`: central workflow state machine and snapshot source for UI/tray.
- `src/BleHotspotClient.*`: EasySpot BLE discovery and GATT write implementation.
- `src/NetworkManagerWifiController.*`: NetworkManager DBus Wi-Fi discovery, volatile activation, and state monitoring.
- `src/SecretServicePasswordStore.*`: Freedesktop Secret Service password storage.
- `src/AutostartManager.*`: XDG autostart file management.
- `src/SettingsStore.*`: non-secret `QSettings` persistence.
- `resources/`: desktop file and Qt resource file.
- `assets/`: application icon assets installed into the hicolor icon theme.
- `packaging/`: Fedora RPM spec and local RPM build notes.
- `tests/`: Qt Test coverage for workflow, UI wiring, NetworkManager settings, Secret Service DBus type registration, and autostart output.

## Behavior Rules

- BLE service UUID: `7baad717-1551-45e1-b852-78d20c7211ec`.
- BLE characteristic UUID: `47436878-5308-40f9-9c29-82c2cb87f595`.
- `Turn Hotspot ON` sends BLE payload `0x01`, waits 5 seconds, then attempts Wi-Fi connection.
- `Connect to Hotspot` never sends BLE.
- `Turn Hotspot OFF` sends BLE payload `0x00` even when Linux is not connected to the target hotspot.
- New operations preempt pending operations.
- When already connected to the configured SSID, disable `Turn Hotspot ON` and `Connect to Hotspot`.
- Password storage must stay in Secret Service/KWallet. Do not add plaintext password persistence.
- NetworkManager receives the hotspot password only for volatile connection activation.

## UI Rules

- Use Qt Designer `.ui` files and stock Qt widgets.
- Do not add stylesheets, custom painting, hard-coded colors, manually drawn controls, or theme-specific palette hacks.
- Let KDE Breeze and the active Qt platform theme control color, light/dark mode, typography, spacing, and widget rendering.
- Prefer `QIcon::fromTheme` with standard Qt icon fallbacks for UI icons.
- Keep UI text concise and user-facing. Put implementation details in debug output or documentation, not in the main workflow screen.

## Development Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For an isolated local validation build, use a separate build directory:

```sh
cmake -S . -B build-codex -DCMAKE_BUILD_TYPE=Debug
cmake --build build-codex
ctest --test-dir build-codex --output-on-failure
```

Fedora package build notes live in `packaging/README.md`. Keep RPM dependency declarations package-name based unless a future compatibility issue requires a versioned constraint.

Keep generated build outputs out of source control.

## Agent Working Rules

- Read the relevant source before editing.
- Prefer existing patterns, helper classes, signals, and snapshot flow.
- Keep changes local to the affected module.
- Do not rewrite UI code in C++ when a `.ui` file is the appropriate source of truth.
- Do not use `nmcli`; use NetworkManager DBus APIs.
- Do not introduce private Qt APIs unless a future task truly requires them.
- Do not edit sibling reference projects unless explicitly requested.
- If a change affects workflow behavior, update or add focused Qt tests.
- If a change affects CLI arguments, install behavior, dependencies, or project conventions, update documentation in the same turn.
- If a change affects installed files, icons, desktop entries, or dependencies, update `packaging/easyspot-linux-client.spec`.

## Architecture Decisions

### BlueZ DBus for BLE

QtBluetooth discovery can find the EasySpot advertisement, but `QLowEnergyController` has proven unreliable with address type and BlueZ device-object mapping on this target environment. The working backend follows the Python `bleak` client more closely:

- `org.bluez.Adapter1.SetDiscoveryFilter`
- `org.bluez.Adapter1.StartDiscovery`
- `org.freedesktop.DBus.ObjectManager.GetManagedObjects`
- `org.bluez.Device1.Connect`
- wait for `ServicesResolved`
- `org.bluez.GattCharacteristic1.WriteValue`

Keep this direct BlueZ DBus path as the primary BLE backend unless there is strong evidence that QtBluetooth has become reliable for this device path.

### NetworkManager DBus for Wi-Fi

Wi-Fi connection uses `AddAndActivateConnection2` with a volatile profile. The connection settings DBus signature must be `a{sa{sv}}`, represented by `NetworkManagerConnectionSettings = QMap<QString, QVariantMap>`. Do not replace this with a plain `QVariantMap`; that risks sending the wrong DBus type.

The app monitors Wi-Fi state through NetworkManager DBus signals and refreshes state on demand when the window or tray menu is opened. Do not reintroduce constant background polling for ordinary status updates. Short activation polling during a connection attempt is acceptable.

### Secret Storage

The hotspot password belongs only in Freedesktop Secret Service/KWallet. `QSettings` may store non-secret preferences such as target SSID, BLE timeout, debug visibility, and launch-at-login preference.

## Pitfalls & Lessons Learned

- BlueZ GATT connection may take longer than the user-facing scan timeout after the EasySpot device is found. Keep scan timeout and GATT connection/write timeout separate.
- Static Qt resources in a static library may not be registered automatically. `main.cpp` explicitly calls `Q_INIT_RESOURCE(easyspot_linux_client)` so app and tray icons resolve.
- XDG autostart must launch with `--tray`; otherwise login startup opens the main window. Existing enabled autostart files are refreshed at app initialization.
- `qt6-qtbase-private-devel` is unnecessary unless future code actually uses private Qt APIs.
- Keep old persistent NetworkManager profiles out of this app’s cleanup path unless explicitly requested. Users can delete old saved Wi-Fi profiles through KDE network settings.

## Agent Memory

### 2026-07-04

- Background: QtBluetooth connected unreliably to the EasySpot Android BLE service even when advertisement discovery succeeded.
- Problem: `QLowEnergyController` timed out or selected the wrong BlueZ address/device path.
- Decision: Use direct BlueZ DBus GATT discovery and `WriteValue` as the primary BLE backend.
- Impact range: `BleHotspotClient`, BLE debug logs, troubleshooting guidance.

### 2026-07-04

- Background: NetworkManager `AddAndActivateConnection2` requires nested connection settings with DBus signature `a{sa{sv}}`.
- Problem: A plain `QVariantMap` can encode as the wrong DBus map type.
- Decision: Use `NetworkManagerConnectionSettings = QMap<QString, QVariantMap>` and register the DBus metatype.
- Impact range: `NetworkManagerWifiController`, NetworkManager unit tests.

### 2026-07-04

- Background: A tray utility should not poll network state constantly while idle.
- Problem: Polling current SSID every 2 seconds is unnecessary background work.
- Decision: Use NetworkManager DBus signals and on-demand refresh when the main window or tray menu opens; keep short activation polling only during connection attempts.
- Impact range: `NetworkManagerWifiController`, `MainWindow`, `TrayController`.
