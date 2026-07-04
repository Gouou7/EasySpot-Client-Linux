#pragma once

#include "AppTypes.h"

#include <QObject>
#include <QSettings>

class SettingsStore final : public QObject
{
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);

    AppSettings load() const;
    void saveTargetSsid(const QString &ssid);
    void saveBleScanTimeoutSeconds(int seconds);
    void saveLaunchAtLoginEnabled(bool enabled);
    void saveDebugInformationEnabled(bool enabled);

private:
    mutable QSettings m_settings;
};
