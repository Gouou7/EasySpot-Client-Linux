#include "SystemLauncher.h"

#include <QDesktopServices>
#include <QUrl>

SystemLauncher::SystemLauncher(QObject *parent)
    : QObject(parent)
{
}

void SystemLauncher::openBluetoothSettings()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("systemsettings:bluetooth")));
}

void SystemLauncher::openNetworkSettings()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("systemsettings:kcm_networkmanagement")));
}
