#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

enum class HotspotCommand
{
    TurnOff,
    TurnOn,
};

enum class BleClientState
{
    Idle,
    Scanning,
    Connecting,
    Writing,
    Success,
    Disconnecting,
    Error,
};

struct AppSettings
{
    QString targetSsid;
    int bleScanTimeoutSeconds = 15;
    bool launchAtLoginEnabled = false;
    bool debugInformationEnabled = false;
};

struct AppState
{
    BleClientState bleState = BleClientState::Idle;
    QString currentSsid;
    bool isConnectedToTarget = false;
    bool isTransitioning = false;
    QString lastError;
    bool isPasswordSaved = false;
    QStringList bleDebugLog;
};

struct ControllerSnapshot
{
    AppSettings settings;
    AppState state;
};

Q_DECLARE_METATYPE(ControllerSnapshot)
