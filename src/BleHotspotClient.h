#pragma once

#include "AppTypes.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QTimer>

class HotspotBleClient : public QObject
{
    Q_OBJECT

public:
    explicit HotspotBleClient(QObject *parent = nullptr) : QObject(parent) {}
    virtual void trigger(HotspotCommand command, int timeoutMs) = 0;

signals:
    void stateChanged(BleClientState state);
    void commandFinished(bool success, const QString &errorMessage);
    void debugMessage(const QString &message);
};

class BleHotspotClient final : public HotspotBleClient
{
    Q_OBJECT

public:
    explicit BleHotspotClient(QObject *parent = nullptr);
    ~BleHotspotClient() override;

    void trigger(HotspotCommand command, int timeoutMs) override;

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void onScanFinished();
    void onControllerConnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicWritten(const QLowEnergyCharacteristic &characteristic, const QByteArray &value);
    void onServiceError(QLowEnergyService::ServiceError error);
    void onBluezPoll();
    void onBluezConnectFinished(QDBusPendingCallWatcher *watcher);
    void onBluezWriteFinished(QDBusPendingCallWatcher *watcher);
    void onTimeout();

private:
    struct BluezDevice
    {
        QString path;
        QString address;
        QString name;
        QStringList uuids;
        bool servicesResolved = false;
    };

    struct BluezCharacteristic
    {
        QString path;
        QStringList flags;
    };

    void resetOperation();
    void finish(bool success, const QString &errorMessage = {});
    void applyState(BleClientState state);
    bool startBluezOperation();
    QString findBluezAdapterPath() const;
    BluezDevice findBluezDevice() const;
    BluezDevice readBluezDevice(const QString &path) const;
    BluezCharacteristic findBluezCharacteristic() const;
    void connectToBluezDevice();
    void writeBluezCommand();
    void stopBluezDiscovery();
    void openDiscoveredService();
    void connectToDiscoveredDevice(QLowEnergyController::RemoteAddressType addressType);
    void restartOperationTimeout(int timeoutMs);
    void debug(const QString &message);

    static const QBluetoothUuid ServiceUuid;
    static const QBluetoothUuid CharacteristicUuid;

    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QTimer m_timeoutTimer;
    QTimer m_bluezPollTimer;
    HotspotCommand m_pendingCommand = HotspotCommand::TurnOff;
    BleClientState m_state = BleClientState::Idle;
    QBluetoothDeviceInfo m_discoveredDevice;
    QString m_bluezAdapterPath;
    QString m_bluezDevicePath;
    QString m_bluezCharacteristicPath;
    QLowEnergyController::RemoteAddressType m_addressType = QLowEnergyController::RandomAddress;
    int m_discoveredDeviceCount = 0;
    bool m_sawLikelyPhone = false;
    bool m_matchedDevice = false;
    bool m_matchedService = false;
    bool m_usingBluez = false;
    bool m_retriedPublicAddress = false;
    int m_timeoutMs = 0;
    bool m_finished = true;
};
