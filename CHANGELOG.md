# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.1.0] - 2026-07-04

### Added

- Native Qt 6 Widgets client for controlling an EasySpot Android hotspot from Fedora/KDE.
- BlueZ DBus BLE backend for discovering the EasySpot service and writing hotspot on/off commands.
- `Turn Hotspot ON`, `Turn Hotspot OFF`, and `Connect to Hotspot` workflows.
- NetworkManager DBus Wi-Fi connection using a volatile activation profile.
- Freedesktop Secret Service/KWallet password storage for hotspot credentials.
- Qt Designer settings UI with status, actions, credentials, system integration, advanced options, about, and debug sections.
- System tray menu with hotspot actions, settings access, launch-at-login toggle, notifications, and quit action.
- XDG autostart support with tray startup mode.
- Application icon resources and hicolor icon installation rules.
- Fedora RPM packaging spec and packaging notes.
- Event-driven Wi-Fi state updates through NetworkManager DBus signals, with on-demand refresh from the window and tray menu.
- Qt Test coverage for workflow behavior, NetworkManager connection settings, Secret Service DBus type registration, autostart file output, and main-window action wiring.
