#include "NetworkManagerWifiController.h"

#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDebug>
#include <QVariantMap>

#include <utility>

namespace
{
constexpr auto NetworkManagerService = "org.freedesktop.NetworkManager";
constexpr auto NetworkManagerPath = "/org/freedesktop/NetworkManager";
constexpr auto NetworkManagerInterface = "org.freedesktop.NetworkManager";
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr uint NmDeviceTypeWifi = 2;
constexpr uint NmActiveConnectionActivated = 2;

QVariant readDbusProperty(const QString &path, const QString &interfaceName, const QString &propertyName)
{
    QDBusInterface properties(NetworkManagerService,
                              path,
                              PropertiesInterface,
                              QDBusConnection::systemBus());
    QDBusReply<QVariant> reply = properties.call(QStringLiteral("Get"), interfaceName, propertyName);
    return reply.isValid() ? reply.value() : QVariant();
}

QString ssidFromBytes(const QVariant &value)
{
    const auto bytes = value.toByteArray();
    if (!bytes.isEmpty()) {
        return QString::fromUtf8(bytes);
    }

    QList<uchar> raw;
    auto argument = value.value<QDBusArgument>();
    if (argument.currentType() == QDBusArgument::ArrayType) {
        argument.beginArray();
        while (!argument.atEnd()) {
            uchar byte = 0;
            argument >> byte;
            raw.append(byte);
        }
        argument.endArray();
    }

    QByteArray data;
    data.reserve(raw.size());
    for (const auto byte : raw) {
        data.append(static_cast<char>(byte));
    }
    return QString::fromUtf8(data);
}

QString formatDbusError(const QDBusMessage &reply, const QString &fallback)
{
    if (reply.type() != QDBusMessage::ErrorMessage || reply.errorMessage().isEmpty()) {
        return fallback;
    }

    return QStringLiteral("%1 (%2)").arg(fallback, reply.errorMessage());
}

void wifiDebug(const QString &message)
{
    qInfo().noquote() << "easyspot.wifi:" << message;
}
}

NetworkManagerWifiController::NetworkManagerWifiController(QObject *parent)
    : WifiController(parent)
{
    qDBusRegisterMetaType<NetworkManagerConnectionSettings>();

    connect(&m_connectPollTimer, &QTimer::timeout, this, &NetworkManagerWifiController::pollPendingConnection);
    m_connectPollTimer.setInterval(500);
}

QString NetworkManagerWifiController::currentSsid() const
{
    return currentSsidUnchecked();
}

void NetworkManagerWifiController::startMonitoring()
{
    if (m_monitoring) {
        return;
    }

    m_monitoring = true;
    QDBusConnection::systemBus().connect(NetworkManagerService,
                                         NetworkManagerPath,
                                         PropertiesInterface,
                                         QStringLiteral("PropertiesChanged"),
                                         this,
                                         SLOT(onNetworkManagerPropertiesChanged(QString,QVariantMap,QStringList)));
    QDBusConnection::systemBus().connect(NetworkManagerService,
                                         NetworkManagerPath,
                                         NetworkManagerInterface,
                                         QStringLiteral("DeviceAdded"),
                                         this,
                                         SLOT(onDeviceAdded(QDBusObjectPath)));
    QDBusConnection::systemBus().connect(NetworkManagerService,
                                         NetworkManagerPath,
                                         NetworkManagerInterface,
                                         QStringLiteral("DeviceRemoved"),
                                         this,
                                         SLOT(onDeviceRemoved(QDBusObjectPath)));
    refreshWifiDeviceSignalSubscriptions();
    m_lastSsid = currentSsidUnchecked();
    emit currentSsidChanged(m_lastSsid);
}

void NetworkManagerWifiController::stopMonitoring()
{
    if (!m_monitoring) {
        return;
    }

    QDBusConnection::systemBus().disconnect(NetworkManagerService,
                                            NetworkManagerPath,
                                            PropertiesInterface,
                                            QStringLiteral("PropertiesChanged"),
                                            this,
                                            SLOT(onNetworkManagerPropertiesChanged(QString,QVariantMap,QStringList)));
    QDBusConnection::systemBus().disconnect(NetworkManagerService,
                                            NetworkManagerPath,
                                            NetworkManagerInterface,
                                            QStringLiteral("DeviceAdded"),
                                            this,
                                            SLOT(onDeviceAdded(QDBusObjectPath)));
    QDBusConnection::systemBus().disconnect(NetworkManagerService,
                                            NetworkManagerPath,
                                            NetworkManagerInterface,
                                            QStringLiteral("DeviceRemoved"),
                                            this,
                                            SLOT(onDeviceRemoved(QDBusObjectPath)));
    for (const auto &devicePath : std::as_const(m_monitoredWifiDevicePaths)) {
        disconnectWifiDeviceSignal(devicePath);
    }
    m_monitoredWifiDevicePaths.clear();
    m_monitoring = false;
    m_connectPollTimer.stop();
}

void NetworkManagerWifiController::connectToTarget(const QString &ssid, const QString &password, int timeoutMs)
{
    m_connectPollTimer.stop();
    wifiDebug(tr("Connect requested: ssid=%1 timeout=%2 ms").arg(ssid, QString::number(timeoutMs)));

    if (ssid.trimmed().isEmpty()) {
        emit connectFinished(false, tr("Save the hotspot SSID before connecting."));
        return;
    }

    const auto wifiDevicePath = findWifiDevicePath();
    if (wifiDevicePath.isEmpty()) {
        wifiDebug(tr("No Wi-Fi device exposed by NetworkManager."));
        emit connectFinished(false, tr("NetworkManager did not expose a Wi-Fi device."));
        return;
    }
    wifiDebug(tr("Using Wi-Fi device %1.").arg(wifiDevicePath));

    const auto accessPointPath = findAccessPointPath(wifiDevicePath, ssid);
    if (accessPointPath.isEmpty()) {
        wifiDebug(tr("Target SSID is not visible in NetworkManager scan results yet."));
        emit connectFinished(false, tr("The configured hotspot SSID is not visible yet."));
        return;
    }
    wifiDebug(tr("Using access point %1 for SSID %2.").arg(accessPointPath, ssid));

    QString errorMessage;
    QString activeConnectionPath;
    if (!addVolatileConnectionAndActivate(ssid, password, wifiDevicePath, accessPointPath, &activeConnectionPath, &errorMessage)) {
        wifiDebug(tr("AddAndActivateConnection2 failed: %1").arg(errorMessage));
        emit connectFinished(false, errorMessage);
        return;
    }
    wifiDebug(tr("Activation started: activeConnection=%1").arg(activeConnectionPath));

    m_pendingSsid = ssid;
    m_pendingActiveConnectionPath = activeConnectionPath;
    m_pendingDeadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    pollPendingConnection();
    if (!m_connectPollTimer.isActive()) {
        m_connectPollTimer.start();
    }
}

void NetworkManagerWifiController::pollCurrentSsid()
{
    const auto ssid = currentSsidUnchecked();
    if (ssid != m_lastSsid) {
        m_lastSsid = ssid;
        emit currentSsidChanged(ssid);
    }
}

void NetworkManagerWifiController::pollPendingConnection()
{
    const auto currentSsid = currentSsidUnchecked();
    if (currentSsid == m_pendingSsid) {
        m_connectPollTimer.stop();
        emit connectFinished(true, {});
        pollCurrentSsid();
        return;
    }

    const auto state = activeConnectionState(m_pendingActiveConnectionPath).toUInt();
    wifiDebug(tr("Polling activation: activeConnection=%1 state=%2 currentSsid=%3")
                  .arg(m_pendingActiveConnectionPath,
                       QString::number(state),
                       currentSsid.isEmpty() ? tr("(not connected)") : currentSsid));
    if (state == NmActiveConnectionActivated) {
        m_connectPollTimer.stop();
        emit connectFinished(true, {});
        pollCurrentSsid();
        return;
    }

    if (QDateTime::currentMSecsSinceEpoch() >= m_pendingDeadline) {
        m_connectPollTimer.stop();
        wifiDebug(tr("Activation timed out for SSID %1.").arg(m_pendingSsid));
        emit connectFinished(false, tr("Linux could not join the hotspot before the timeout."));
    }
}

void NetworkManagerWifiController::onNetworkManagerPropertiesChanged(const QString &interfaceName,
                                                                     const QVariantMap &changedProperties,
                                                                     const QStringList &invalidatedProperties)
{
    if (interfaceName != QString::fromLatin1(NetworkManagerInterface)) {
        return;
    }

    if (changedProperties.contains(QStringLiteral("ActiveConnections")) ||
        changedProperties.contains(QStringLiteral("PrimaryConnection")) ||
        changedProperties.contains(QStringLiteral("State")) ||
        invalidatedProperties.contains(QStringLiteral("ActiveConnections")) ||
        invalidatedProperties.contains(QStringLiteral("PrimaryConnection"))) {
        pollCurrentSsid();
    }
}

void NetworkManagerWifiController::onDevicePropertiesChanged(const QString &interfaceName,
                                                             const QVariantMap &changedProperties,
                                                             const QStringList &invalidatedProperties)
{
    if (interfaceName != QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless") &&
        interfaceName != QStringLiteral("org.freedesktop.NetworkManager.Device")) {
        return;
    }

    if (changedProperties.contains(QStringLiteral("ActiveAccessPoint")) ||
        changedProperties.contains(QStringLiteral("State")) ||
        invalidatedProperties.contains(QStringLiteral("ActiveAccessPoint"))) {
        pollCurrentSsid();
    }
}

void NetworkManagerWifiController::onDeviceAdded(const QDBusObjectPath &)
{
    refreshWifiDeviceSignalSubscriptions();
    pollCurrentSsid();
}

void NetworkManagerWifiController::onDeviceRemoved(const QDBusObjectPath &)
{
    refreshWifiDeviceSignalSubscriptions();
    pollCurrentSsid();
}

QString NetworkManagerWifiController::currentSsidUnchecked() const
{
    QDBusInterface manager(NetworkManagerService,
                           NetworkManagerPath,
                           NetworkManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<QList<QDBusObjectPath>> reply = manager.call(QStringLiteral("GetDevices"));
    if (!reply.isValid()) {
        return {};
    }

    for (const auto &device : reply.value()) {
        const auto path = device.path();
        if (readDbusProperty(path, QStringLiteral("org.freedesktop.NetworkManager.Device"), QStringLiteral("DeviceType")).toUInt() != NmDeviceTypeWifi) {
            continue;
        }

        const auto activeAp = readDbusProperty(path,
                                               QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"),
                                               QStringLiteral("ActiveAccessPoint")).value<QDBusObjectPath>().path();
        if (activeAp.isEmpty() || activeAp == QStringLiteral("/")) {
            continue;
        }

        return ssidFromBytes(readDbusProperty(activeAp,
                                              QStringLiteral("org.freedesktop.NetworkManager.AccessPoint"),
                                              QStringLiteral("Ssid")));
    }

    return {};
}

QString NetworkManagerWifiController::findWifiDevicePath() const
{
    const auto devicePaths = findWifiDevicePaths();
    return devicePaths.isEmpty() ? QString() : devicePaths.first();
}

QStringList NetworkManagerWifiController::findWifiDevicePaths() const
{
    QDBusInterface manager(NetworkManagerService,
                           NetworkManagerPath,
                           NetworkManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<QList<QDBusObjectPath>> reply = manager.call(QStringLiteral("GetDevices"));
    if (!reply.isValid()) {
        return {};
    }

    QStringList devicePaths;
    for (const auto &device : reply.value()) {
        const auto path = device.path();
        if (readDbusProperty(path, QStringLiteral("org.freedesktop.NetworkManager.Device"), QStringLiteral("DeviceType")).toUInt() == NmDeviceTypeWifi) {
            devicePaths.append(path);
        }
    }

    return devicePaths;
}

QString NetworkManagerWifiController::findAccessPointPath(const QString &wifiDevicePath, const QString &ssid) const
{
    QDBusInterface wireless(NetworkManagerService,
                            wifiDevicePath,
                            QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"),
                            QDBusConnection::systemBus());
    wireless.call(QStringLiteral("RequestScan"), QVariantMap());
    wifiDebug(tr("Requested Wi-Fi scan on %1.").arg(wifiDevicePath));

    QDBusReply<QList<QDBusObjectPath>> reply = wireless.call(QStringLiteral("GetAccessPoints"));
    if (!reply.isValid()) {
        wifiDebug(tr("GetAccessPoints failed: %1").arg(reply.error().message()));
        return {};
    }
    wifiDebug(tr("NetworkManager returned %1 visible access points.").arg(QString::number(reply.value().size())));

    for (const auto &accessPoint : reply.value()) {
        const auto accessPointPath = accessPoint.path();
        const auto accessPointSsid = ssidFromBytes(readDbusProperty(accessPointPath,
                                                                    QStringLiteral("org.freedesktop.NetworkManager.AccessPoint"),
                                                                    QStringLiteral("Ssid")));
        if (accessPointSsid == ssid) {
            wifiDebug(tr("Matched access point %1 for SSID %2.").arg(accessPointPath, ssid));
            return accessPointPath;
        }
    }

    return {};
}

QString NetworkManagerWifiController::activeConnectionState(const QString &activeConnectionPath) const
{
    if (activeConnectionPath.isEmpty()) {
        return {};
    }

    return readDbusProperty(activeConnectionPath,
                            QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                            QStringLiteral("State")).toString();
}

void NetworkManagerWifiController::refreshWifiDeviceSignalSubscriptions()
{
    const auto currentDevicePaths = findWifiDevicePaths();
    for (const auto &devicePath : m_monitoredWifiDevicePaths) {
        if (!currentDevicePaths.contains(devicePath)) {
            disconnectWifiDeviceSignal(devicePath);
        }
    }

    for (const auto &devicePath : currentDevicePaths) {
        if (!m_monitoredWifiDevicePaths.contains(devicePath)) {
            connectWifiDeviceSignal(devicePath);
        }
    }

    m_monitoredWifiDevicePaths = currentDevicePaths;
}

void NetworkManagerWifiController::connectWifiDeviceSignal(const QString &devicePath)
{
    QDBusConnection::systemBus().connect(NetworkManagerService,
                                         devicePath,
                                         PropertiesInterface,
                                         QStringLiteral("PropertiesChanged"),
                                         this,
                                         SLOT(onDevicePropertiesChanged(QString,QVariantMap,QStringList)));
}

void NetworkManagerWifiController::disconnectWifiDeviceSignal(const QString &devicePath)
{
    QDBusConnection::systemBus().disconnect(NetworkManagerService,
                                            devicePath,
                                            PropertiesInterface,
                                            QStringLiteral("PropertiesChanged"),
                                            this,
                                            SLOT(onDevicePropertiesChanged(QString,QVariantMap,QStringList)));
}

QVariantMap NetworkManagerWifiController::buildVolatileConnectionSettings(const QString &ssid, const QString &password)
{
    const auto settings = buildVolatileConnectionSettingsForDbus(ssid, password);
    QVariantMap values;
    for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
        values.insert(it.key(), it.value());
    }
    return values;
}

NetworkManagerConnectionSettings NetworkManagerWifiController::buildVolatileConnectionSettingsForDbus(const QString &ssid, const QString &password)
{
    QVariantMap connection;
    connection.insert(QStringLiteral("id"), QStringLiteral("EasySpot %1").arg(ssid));
    connection.insert(QStringLiteral("type"), QStringLiteral("802-11-wireless"));
    connection.insert(QStringLiteral("autoconnect"), false);

    QVariantMap wireless;
    wireless.insert(QStringLiteral("ssid"), ssid.toUtf8());
    wireless.insert(QStringLiteral("mode"), QStringLiteral("infrastructure"));
    wireless.insert(QStringLiteral("security"), QStringLiteral("802-11-wireless-security"));

    QVariantMap security;
    security.insert(QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk"));
    security.insert(QStringLiteral("psk"), password);

    QVariantMap ipv4;
    ipv4.insert(QStringLiteral("method"), QStringLiteral("auto"));

    QVariantMap ipv6;
    ipv6.insert(QStringLiteral("method"), QStringLiteral("auto"));

    NetworkManagerConnectionSettings settings;
    settings.insert(QStringLiteral("connection"), connection);
    settings.insert(QStringLiteral("802-11-wireless"), wireless);
    settings.insert(QStringLiteral("802-11-wireless-security"), security);
    settings.insert(QStringLiteral("ipv4"), ipv4);
    settings.insert(QStringLiteral("ipv6"), ipv6);
    return settings;
}

QVariantMap NetworkManagerWifiController::buildVolatileActivationOptions()
{
    QVariantMap options;
    options.insert(QStringLiteral("persist"), QStringLiteral("volatile"));
    return options;
}

bool NetworkManagerWifiController::addVolatileConnectionAndActivate(const QString &ssid,
                                                                    const QString &password,
                                                                    const QString &wifiDevicePath,
                                                                    const QString &accessPointPath,
                                                                    QString *activeConnectionPath,
                                                                    QString *errorMessage) const
{
    const auto settings = buildVolatileConnectionSettings(ssid, password);
    const auto dbusSettings = buildVolatileConnectionSettingsForDbus(ssid, password);
    const auto options = buildVolatileActivationOptions();

    QDBusInterface manager(NetworkManagerService,
                           NetworkManagerPath,
                           NetworkManagerInterface,
                           QDBusConnection::systemBus());

    QDBusMessage reply = manager.call(QStringLiteral("AddAndActivateConnection2"),
                                      QVariant::fromValue(dbusSettings),
                                      QVariant::fromValue(QDBusObjectPath(wifiDevicePath)),
                                      QVariant::fromValue(QDBusObjectPath(accessPointPath)),
                                      options);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (errorMessage) {
            *errorMessage = formatDbusError(reply,
                                            tr("NetworkManager does not support volatile EasySpot hotspot connections."));
        }
        return false;
    }

    *activeConnectionPath = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
    return true;
}
