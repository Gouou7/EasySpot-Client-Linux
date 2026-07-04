#pragma once

#include <QObject>
#include <QTimer>
#include <QDBusObjectPath>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

using NetworkManagerConnectionSettings = QMap<QString, QVariantMap>;

Q_DECLARE_METATYPE(NetworkManagerConnectionSettings)

class WifiController : public QObject
{
    Q_OBJECT

public:
    explicit WifiController(QObject *parent = nullptr) : QObject(parent) {}

    virtual QString currentSsid() const = 0;
    virtual void startMonitoring() = 0;
    virtual void stopMonitoring() = 0;
    virtual void connectToTarget(const QString &ssid, const QString &password, int timeoutMs) = 0;

signals:
    void currentSsidChanged(const QString &ssid);
    void connectFinished(bool success, const QString &errorMessage);
};

class NetworkManagerWifiController final : public WifiController
{
    Q_OBJECT

public:
    explicit NetworkManagerWifiController(QObject *parent = nullptr);

    QString currentSsid() const override;
    void startMonitoring() override;
    void stopMonitoring() override;
    void connectToTarget(const QString &ssid, const QString &password, int timeoutMs) override;

    static QVariantMap buildVolatileConnectionSettings(const QString &ssid, const QString &password);
    static NetworkManagerConnectionSettings buildVolatileConnectionSettingsForDbus(const QString &ssid, const QString &password);
    static QVariantMap buildVolatileActivationOptions();

private slots:
    void pollCurrentSsid();
    void pollPendingConnection();
    void onNetworkManagerPropertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);
    void onDevicePropertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);
    void onDeviceAdded(const QDBusObjectPath &devicePath);
    void onDeviceRemoved(const QDBusObjectPath &devicePath);

private:
    QString currentSsidUnchecked() const;
    QString findWifiDevicePath() const;
    QStringList findWifiDevicePaths() const;
    QString findAccessPointPath(const QString &wifiDevicePath, const QString &ssid) const;
    QString activeConnectionState(const QString &activeConnectionPath) const;
    void refreshWifiDeviceSignalSubscriptions();
    void connectWifiDeviceSignal(const QString &devicePath);
    void disconnectWifiDeviceSignal(const QString &devicePath);
    bool addVolatileConnectionAndActivate(const QString &ssid,
                                          const QString &password,
                                          const QString &wifiDevicePath,
                                          const QString &accessPointPath,
                                          QString *activeConnectionPath,
                                          QString *errorMessage) const;

    QTimer m_connectPollTimer;
    QStringList m_monitoredWifiDevicePaths;
    QString m_lastSsid;
    QString m_pendingSsid;
    QString m_pendingActiveConnectionPath;
    bool m_monitoring = false;
    qint64 m_pendingDeadline = 0;
};
