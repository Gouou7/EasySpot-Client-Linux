#include "AutostartManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

AutostartManager::AutostartManager(QObject *parent)
    : QObject(parent)
{
}

bool AutostartManager::isEnabled() const
{
    return QFileInfo::exists(autostartFilePath());
}

bool AutostartManager::setEnabled(bool enabled, QString *errorMessage)
{
    const auto filePath = autostartFilePath();
    if (!enabled) {
        if (QFileInfo::exists(filePath) && !QFile::remove(filePath)) {
            if (errorMessage) {
                *errorMessage = tr("Could not remove the EasySpot autostart entry.");
            }
            return false;
        }
        return true;
    }

    const auto dirPath = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        if (errorMessage) {
            *errorMessage = tr("Could not create the XDG autostart directory.");
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = tr("Could not write the EasySpot autostart entry.");
        }
        return false;
    }

    QTextStream stream(&file);
    stream << "[Desktop Entry]\n";
    stream << "Type=Application\n";
    stream << "Name=EasySpot Linux Client\n";
    stream << "Comment=Control an Android EasySpot hotspot from KDE\n";
    stream << "Exec=\"" << executablePath() << "\" --tray\n";
    stream << "Icon=easyspot-linux-client\n";
    stream << "Hidden=false\n";
    stream << "Terminal=false\n";
    stream << "X-GNOME-Autostart-enabled=true\n";
    stream << "X-KDE-autostart-after=panel\n";
    stream << "OnlyShowIn=KDE;GNOME;XFCE;LXQt;\n";
    return true;
}

QString AutostartManager::autostartFilePath()
{
    const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configRoot).filePath(QStringLiteral("autostart/easyspot-linux-client.desktop"));
}

QString AutostartManager::executablePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}
