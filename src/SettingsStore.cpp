#include "SettingsStore.h"

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent),
      m_settings(QStringLiteral("EasySpot"), QStringLiteral("EasySpotLinuxClient"))
{
}

AppSettings SettingsStore::load() const
{
    AppSettings settings;
    settings.targetSsid = m_settings.value(QStringLiteral("targetSsid")).toString();
    settings.bleScanTimeoutSeconds = m_settings.value(QStringLiteral("bleScanTimeoutSeconds"), 15).toInt();
    if (settings.bleScanTimeoutSeconds != 10 &&
        settings.bleScanTimeoutSeconds != 15 &&
        settings.bleScanTimeoutSeconds != 20 &&
        settings.bleScanTimeoutSeconds != 30) {
        settings.bleScanTimeoutSeconds = 15;
    }

    settings.launchAtLoginEnabled = m_settings.value(QStringLiteral("launchAtLoginEnabled"), false).toBool();
    settings.debugInformationEnabled = m_settings.value(QStringLiteral("debugInformationEnabled"), false).toBool();
    return settings;
}

void SettingsStore::saveTargetSsid(const QString &ssid)
{
    m_settings.setValue(QStringLiteral("targetSsid"), ssid.trimmed());
}

void SettingsStore::saveBleScanTimeoutSeconds(int seconds)
{
    m_settings.setValue(QStringLiteral("bleScanTimeoutSeconds"), seconds);
}

void SettingsStore::saveLaunchAtLoginEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("launchAtLoginEnabled"), enabled);
}

void SettingsStore::saveDebugInformationEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("debugInformationEnabled"), enabled);
}
