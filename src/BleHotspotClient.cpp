#include "BleHotspotClient.h"

#include <QBluetoothLocalDevice>
#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>
#include <QVariantMap>

const QBluetoothUuid BleHotspotClient::ServiceUuid(QStringLiteral("7baad717-1551-45e1-b852-78d20c7211ec"));
const QBluetoothUuid BleHotspotClient::CharacteristicUuid(QStringLiteral("47436878-5308-40f9-9c29-82c2cb87f595"));

using BluezInterfaceMap = QMap<QString, QVariantMap>;
using BluezManagedObjects = QMap<QDBusObjectPath, BluezInterfaceMap>;

Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezManagedObjects)

namespace
{
constexpr auto BluezService = "org.bluez";
constexpr auto ObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto AdapterInterface = "org.bluez.Adapter1";
constexpr auto DeviceInterface = "org.bluez.Device1";
constexpr auto GattCharacteristicInterface = "org.bluez.GattCharacteristic1";

QString discoveryErrorMessage(QBluetoothDeviceDiscoveryAgent::Error error)
{
    switch (error) {
    case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
        return QObject::tr("Bluetooth is turned off. Enable it in KDE Bluetooth settings and try again.");
    case QBluetoothDeviceDiscoveryAgent::InvalidBluetoothAdapterError:
        return QObject::tr("Linux does not expose a usable Bluetooth adapter.");
    case QBluetoothDeviceDiscoveryAgent::UnsupportedDiscoveryMethod:
        return QObject::tr("This Bluetooth adapter does not support BLE scanning.");
    case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
        return QObject::tr("Linux could not access Bluetooth. Check desktop Bluetooth permissions and try again.");
    case QBluetoothDeviceDiscoveryAgent::InputOutputError:
        return QObject::tr("Bluetooth scanning failed because BlueZ reported an input/output error.");
    default:
        return QObject::tr("Bluetooth scanning failed. Check BlueZ, adapter power, and desktop Bluetooth permissions.");
    }
}

QString uuidListText(const QList<QBluetoothUuid> &uuids)
{
    QStringList values;
    values.reserve(uuids.size());
    for (const auto &uuid : uuids) {
        values.append(uuid.toString(QUuid::WithoutBraces));
    }
    return values.isEmpty() ? QStringLiteral("(none)") : values.join(QStringLiteral(", "));
}

QString addressTypeText(QLowEnergyController::RemoteAddressType addressType)
{
    return addressType == QLowEnergyController::RandomAddress ? QStringLiteral("random") : QStringLiteral("public");
}

int bluezGattTimeoutMs(int requestedTimeoutMs)
{
    constexpr int MinGattTimeoutMs = 30000;
    return qMax(MinGattTimeoutMs, requestedTimeoutMs);
}

bool uuidListContains(const QStringList &uuids, const QBluetoothUuid &uuid)
{
    const auto target = uuid.toString(QUuid::WithoutBraces).toLower();
    for (const auto &value : uuids) {
        if (value.toLower() == target) {
            return true;
        }
    }
    return false;
}

BluezManagedObjects managedObjects()
{
    QDBusInterface manager(BluezService,
                           QStringLiteral("/"),
                           ObjectManagerInterface,
                           QDBusConnection::systemBus());
    const QDBusMessage reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    return qdbus_cast<BluezManagedObjects>(reply.arguments().first().value<QDBusArgument>());
}

QString formatDbusError(QDBusPendingCallWatcher *watcher, const QString &fallback)
{
    const auto reply = watcher->reply();
    if (reply.type() != QDBusMessage::ErrorMessage || reply.errorMessage().isEmpty()) {
        return fallback;
    }

    return QStringLiteral("%1 (%2)").arg(fallback, reply.errorMessage());
}
}

BleHotspotClient::BleHotspotClient(QObject *parent)
    : HotspotBleClient(parent)
{
    qDBusRegisterMetaType<BluezInterfaceMap>();
    qDBusRegisterMetaType<BluezManagedObjects>();

    m_timeoutTimer.setSingleShot(true);
    m_bluezPollTimer.setInterval(250);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &BleHotspotClient::onTimeout);
    connect(&m_bluezPollTimer, &QTimer::timeout, this, &BleHotspotClient::onBluezPoll);
}

BleHotspotClient::~BleHotspotClient()
{
    resetOperation();
}

void BleHotspotClient::trigger(HotspotCommand command, int timeoutMs)
{
    resetOperation();
    m_finished = false;
    m_pendingCommand = command;
    m_usingBluez = true;
    m_discoveredDeviceCount = 0;
    m_sawLikelyPhone = false;
    m_matchedDevice = false;
    m_matchedService = false;
    m_retriedPublicAddress = false;
    m_timeoutMs = timeoutMs;

    debug(tr("BLE trigger requested: command=%1 timeout=%2 ms")
              .arg(command == HotspotCommand::TurnOn ? QStringLiteral("TurnOn") : QStringLiteral("TurnOff"))
              .arg(timeoutMs));

    if (!startBluezOperation()) {
        finish(false, tr("BlueZ did not expose a usable Bluetooth Low Energy adapter."));
    }
    return;

    if (!QBluetoothDeviceDiscoveryAgent::supportedDiscoveryMethods().testFlag(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod)) {
        debug(tr("BLE discovery method check failed: LowEnergyMethod is not supported."));
        finish(false, tr("This Linux device does not expose a usable Bluetooth Low Energy adapter."));
        return;
    }

    QBluetoothLocalDevice localDevice;
    if (!localDevice.isValid()) {
        debug(tr("Local Bluetooth adapter is invalid."));
        finish(false, tr("Linux does not expose a usable Bluetooth adapter."));
        return;
    }

    debug(tr("Local Bluetooth adapter: name=%1 address=%2 hostMode=%3")
              .arg(localDevice.name().isEmpty() ? QStringLiteral("(unnamed)") : localDevice.name(),
                   localDevice.address().toString(),
                   QString::number(static_cast<int>(localDevice.hostMode()))));

    if (localDevice.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        finish(false, tr("Bluetooth is turned off. Enable it in KDE Bluetooth settings and try again."));
        return;
    }

    applyState(command == HotspotCommand::TurnOff ? BleClientState::Disconnecting : BleClientState::Scanning);

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(timeoutMs);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleHotspotClient::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleHotspotClient::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BleHotspotClient::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error error) {
                debug(tr("BLE scan error: code=%1 message=%2")
                          .arg(static_cast<int>(error))
                          .arg(m_discoveryAgent != nullptr ? m_discoveryAgent->errorString() : QString()));
                finish(false, discoveryErrorMessage(error));
            });

    debug(tr("Starting BLE scan for service %1.").arg(ServiceUuid.toString(QUuid::WithoutBraces)));
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleHotspotClient::onDeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    ++m_discoveredDeviceCount;
    if (device.name().contains(QStringLiteral("Titan"), Qt::CaseInsensitive) ||
        device.name().contains(QStringLiteral("Phone"), Qt::CaseInsensitive)) {
        m_sawLikelyPhone = true;
    }

    debug(tr("BLE device discovered: address=%1 uuid=%2 name=%3 rssi=%4 services=%5")
              .arg(device.address().toString(),
                   device.deviceUuid().toString(QUuid::WithoutBraces),
                   device.name().isEmpty() ? QStringLiteral("(unnamed)") : device.name(),
                   QString::number(device.rssi()),
                   uuidListText(device.serviceUuids())));

    if (!device.serviceUuids().contains(ServiceUuid)) {
        return;
    }

    debug(tr("Matched EasySpot BLE service on discovered device."));
    m_discoveredDevice = device;
    m_matchedDevice = true;
    m_discoveryAgent->stop();
    connectToDiscoveredDevice(QLowEnergyController::RandomAddress);
}

void BleHotspotClient::onScanFinished()
{
    debug(tr("BLE scan finished: finished=%1 controllerCreated=%2")
              .arg(m_finished ? QStringLiteral("yes") : QStringLiteral("no"),
                   m_controller == nullptr ? QStringLiteral("no") : QStringLiteral("yes")));
    if (!m_finished && m_controller == nullptr && !m_matchedDevice) {
        debug(tr("BLE scan completed without EasySpot match: discoveredDevices=%1 sawLikelyPhone=%2")
                  .arg(m_discoveredDeviceCount)
                  .arg(m_sawLikelyPhone ? QStringLiteral("yes") : QStringLiteral("no")));
        finish(false, tr("No nearby EasySpot Android device was found over BLE."));
    }
}

void BleHotspotClient::onControllerConnected()
{
    debug(tr("BLE controller connected. Discovering services."));
    restartOperationTimeout(qMax(10000, m_timeoutMs));
    m_controller->discoverServices();
}

void BleHotspotClient::onControllerError(QLowEnergyController::Error)
{
    const auto errorCode = m_controller != nullptr ? m_controller->error() : QLowEnergyController::UnknownError;
    const auto errorString = m_controller != nullptr ? m_controller->errorString() : QString();
    debug(tr("BLE controller error: code=%1 message=%2 state=%3 addressType=%4")
              .arg(static_cast<int>(errorCode))
              .arg(errorString)
              .arg(m_controller != nullptr ? QString::number(static_cast<int>(m_controller->state())) : QStringLiteral("(none)"))
              .arg(addressTypeText(m_addressType)));

    if (m_state == BleClientState::Connecting &&
        m_addressType == QLowEnergyController::RandomAddress &&
        !m_retriedPublicAddress) {
        m_retriedPublicAddress = true;
        debug(tr("Retrying BLE connection with public address type."));
        if (m_controller != nullptr) {
            m_controller->deleteLater();
            m_controller = nullptr;
        }
        connectToDiscoveredDevice(QLowEnergyController::PublicAddress);
        return;
    }

    if (errorCode == QLowEnergyController::AuthorizationError ||
        errorCode == QLowEnergyController::MissingPermissionsError) {
        finish(false, tr("Linux could not access the encrypted EasySpot BLE characteristic. Pair the Android phone in Bluetooth settings first."));
        return;
    }

    finish(false, tr("The EasySpot Android device could not be opened over BLE. Pair the phone and try again."));
}

void BleHotspotClient::onServiceDiscovered(const QBluetoothUuid &serviceUuid)
{
    debug(tr("BLE service discovered: %1").arg(serviceUuid.toString(QUuid::WithoutBraces)));
    if (serviceUuid == ServiceUuid) {
        m_matchedService = true;
    }
}

void BleHotspotClient::onDiscoveryFinished()
{
    debug(tr("BLE service discovery finished. Services=%1").arg(uuidListText(m_controller != nullptr ? m_controller->services() : QList<QBluetoothUuid>())));
    if (m_finished || m_service != nullptr) {
        return;
    }

    if (m_matchedService || (m_controller != nullptr && m_controller->services().contains(ServiceUuid))) {
        openDiscoveredService();
        return;
    }

    if (!m_finished) {
        finish(false, tr("The EasySpot BLE service was not found on the Android device."));
    }
}

void BleHotspotClient::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    debug(tr("BLE service state changed: %1").arg(static_cast<int>(state)));
    if (state != QLowEnergyService::RemoteServiceDiscovered || m_service == nullptr) {
        return;
    }

    const auto characteristic = m_service->characteristic(CharacteristicUuid);
    if (!characteristic.isValid()) {
        debug(tr("EasySpot characteristic is missing after service detail discovery."));
        finish(false, tr("The EasySpot BLE command characteristic is missing on the Android device."));
        return;
    }

    applyState(BleClientState::Writing);
    const QByteArray payload(1, m_pendingCommand == HotspotCommand::TurnOn ? '\x01' : '\x00');
    debug(tr("EasySpot characteristic ready: properties=%1 payload=%2")
              .arg(static_cast<int>(characteristic.properties()))
              .arg(QString::fromLatin1(payload.toHex(' '))));

    if (characteristic.properties().testFlag(QLowEnergyCharacteristic::Write)) {
        debug(tr("Writing EasySpot command with response, matching the Python reference client."));
        m_service->writeCharacteristic(characteristic, payload, QLowEnergyService::WriteWithResponse);
        return;
    }

    if (!characteristic.properties().testFlag(QLowEnergyCharacteristic::WriteNoResponse)) {
        finish(false, tr("The EasySpot BLE command characteristic is not writable on the Android device."));
        return;
    }

    debug(tr("Characteristic lacks write-with-response; writing EasySpot command without response."));
    m_service->writeCharacteristic(characteristic, payload, QLowEnergyService::WriteWithoutResponse);
    finish(true);
}

void BleHotspotClient::onCharacteristicWritten(const QLowEnergyCharacteristic &characteristic, const QByteArray &)
{
    if (characteristic.uuid() == CharacteristicUuid) {
        debug(tr("EasySpot BLE command write acknowledged."));
        finish(true);
    }
}

void BleHotspotClient::onServiceError(QLowEnergyService::ServiceError error)
{
    debug(tr("BLE service error: code=%1").arg(static_cast<int>(error)));
    finish(false, tr("Linux failed to write the EasySpot BLE command."));
}

void BleHotspotClient::onBluezPoll()
{
    if (!m_usingBluez || m_finished) {
        return;
    }

    if (m_state == BleClientState::Scanning) {
        const auto device = findBluezDevice();
        if (device.path.isEmpty()) {
            return;
        }

        m_bluezDevicePath = device.path;
        debug(tr("Matched EasySpot BlueZ device: path=%1 address=%2 name=%3 services=%4")
                  .arg(device.path,
                       device.address.isEmpty() ? QStringLiteral("(unknown)") : device.address,
                       device.name.isEmpty() ? QStringLiteral("(unnamed)") : device.name,
                       device.uuids.isEmpty() ? QStringLiteral("(none)") : device.uuids.join(QStringLiteral(", "))));
        m_bluezPollTimer.stop();
        stopBluezDiscovery();
        connectToBluezDevice();
        return;
    }

    if (m_state == BleClientState::Connecting) {
        const auto device = readBluezDevice(m_bluezDevicePath);
        if (!device.servicesResolved) {
            return;
        }

        debug(tr("BlueZ services resolved for %1.").arg(m_bluezDevicePath));
        writeBluezCommand();
    }
}

void BleHotspotClient::onBluezConnectFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    if (!m_usingBluez || m_finished) {
        return;
    }

    const auto reply = watcher->reply();
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const auto errorName = reply.errorName();
        if (errorName != QStringLiteral("org.bluez.Error.AlreadyConnected") &&
            errorName != QStringLiteral("org.bluez.Error.InProgress")) {
            debug(tr("BlueZ Connect failed: name=%1 message=%2").arg(errorName, reply.errorMessage()));
            finish(false, formatDbusError(watcher, tr("The EasySpot Android device could not be opened over BLE.")));
            return;
        }
        debug(tr("BlueZ Connect returned %1; continuing to wait for services.").arg(errorName));
    } else {
        debug(tr("BlueZ Connect returned successfully; waiting for GATT services."));
    }

    applyState(BleClientState::Connecting);
    if (!m_bluezPollTimer.isActive()) {
        m_bluezPollTimer.start();
    }
}

void BleHotspotClient::onBluezWriteFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    if (!m_usingBluez || m_finished) {
        return;
    }

    const auto reply = watcher->reply();
    if (reply.type() == QDBusMessage::ErrorMessage) {
        debug(tr("BlueZ WriteValue failed: name=%1 message=%2").arg(reply.errorName(), reply.errorMessage()));
        finish(false, formatDbusError(watcher, tr("Linux failed to write the EasySpot BLE command.")));
        return;
    }

    debug(tr("BlueZ WriteValue completed successfully."));
    finish(true);
}

void BleHotspotClient::onTimeout()
{
    switch (m_state) {
    case BleClientState::Scanning:
        if (m_usingBluez) {
            finish(false, tr("No nearby EasySpot Android device was found over BLE."));
            return;
        }
        debug(tr("BLE scan timer elapsed while Qt discovery is still active; waiting for BlueZ scan completion."));
        return;
    case BleClientState::Connecting:
        if (m_usingBluez) {
            finish(false, tr("Linux found the EasySpot Android device, but the BlueZ GATT connection timed out."));
            return;
        }
        debug(tr("BLE connection attempt timed out: addressType=%1").arg(addressTypeText(m_addressType)));
        if (m_addressType == QLowEnergyController::RandomAddress && !m_retriedPublicAddress) {
            m_retriedPublicAddress = true;
            debug(tr("Retrying BLE connection timeout with public address type."));
            if (m_controller != nullptr) {
                m_controller->disconnectFromDevice();
                m_controller->deleteLater();
                m_controller = nullptr;
            }
            connectToDiscoveredDevice(QLowEnergyController::PublicAddress);
            restartOperationTimeout(m_timeoutMs);
            return;
        }
        finish(false, tr("Linux found the EasySpot Android device, but the BLE connection timed out."));
        return;
    case BleClientState::Writing:
        finish(false, tr("Linux connected to the EasySpot BLE service, but service details or command writing timed out."));
        return;
    default:
        finish(false, tr("The EasySpot BLE operation timed out."));
        return;
    }
}

void BleHotspotClient::resetOperation()
{
    m_timeoutTimer.stop();
    m_bluezPollTimer.stop();
    if (m_usingBluez) {
        stopBluezDiscovery();
        if (!m_bluezDevicePath.isEmpty()) {
            QDBusInterface device(BluezService,
                                  m_bluezDevicePath,
                                  DeviceInterface,
                                  QDBusConnection::systemBus());
            device.call(QStringLiteral("Disconnect"));
        }
    }

    if (m_discoveryAgent != nullptr) {
        m_discoveryAgent->stop();
        m_discoveryAgent->deleteLater();
        m_discoveryAgent = nullptr;
    }

    if (m_service != nullptr) {
        m_service->deleteLater();
        m_service = nullptr;
    }

    if (m_controller != nullptr) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
    m_discoveredDevice = QBluetoothDeviceInfo();
    m_bluezAdapterPath.clear();
    m_bluezDevicePath.clear();
    m_bluezCharacteristicPath.clear();
    m_discoveredDeviceCount = 0;
    m_sawLikelyPhone = false;
    m_matchedDevice = false;
    m_matchedService = false;
    m_usingBluez = false;
    m_timeoutMs = 0;
}

void BleHotspotClient::finish(bool success, const QString &errorMessage)
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    m_timeoutTimer.stop();
    debug(success ? tr("BLE operation finished successfully.") : tr("BLE operation failed: %1").arg(errorMessage));
    applyState(success ? BleClientState::Success : BleClientState::Error);
    emit commandFinished(success, errorMessage);
    resetOperation();
}

void BleHotspotClient::applyState(BleClientState state)
{
    m_state = state;
    emit stateChanged(state);
    debug(tr("BLE state changed: %1").arg(static_cast<int>(state)));
}

bool BleHotspotClient::startBluezOperation()
{
    m_bluezAdapterPath = findBluezAdapterPath();
    if (m_bluezAdapterPath.isEmpty()) {
        debug(tr("BlueZ adapter lookup failed."));
        return false;
    }

    applyState(BleClientState::Scanning);
    restartOperationTimeout(m_timeoutMs);

    QDBusInterface adapter(BluezService,
                           m_bluezAdapterPath,
                           AdapterInterface,
                           QDBusConnection::systemBus());

    QVariantMap filter;
    filter.insert(QStringLiteral("UUIDs"), QStringList{ServiceUuid.toString(QUuid::WithoutBraces)});
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    const auto filterReply = adapter.call(QStringLiteral("SetDiscoveryFilter"), filter);
    if (filterReply.type() == QDBusMessage::ErrorMessage) {
        debug(tr("BlueZ SetDiscoveryFilter failed: name=%1 message=%2").arg(filterReply.errorName(), filterReply.errorMessage()));
    }

    const auto startReply = adapter.call(QStringLiteral("StartDiscovery"));
    if (startReply.type() == QDBusMessage::ErrorMessage &&
        startReply.errorName() != QStringLiteral("org.bluez.Error.InProgress")) {
        debug(tr("BlueZ StartDiscovery failed: name=%1 message=%2").arg(startReply.errorName(), startReply.errorMessage()));
        return false;
    }

    debug(tr("Starting BlueZ scan on adapter %1 for service %2.")
              .arg(m_bluezAdapterPath, ServiceUuid.toString(QUuid::WithoutBraces)));
    m_bluezPollTimer.start();
    onBluezPoll();
    return true;
}

QString BleHotspotClient::findBluezAdapterPath() const
{
    const auto objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it.value().contains(QString::fromLatin1(AdapterInterface))) {
            return it.key().path();
        }
    }
    return {};
}

BleHotspotClient::BluezDevice BleHotspotClient::findBluezDevice() const
{
    const auto objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto interfaces = it.value();
        const auto deviceProperties = interfaces.value(QString::fromLatin1(DeviceInterface));
        if (deviceProperties.isEmpty()) {
            continue;
        }

        const auto uuids = deviceProperties.value(QStringLiteral("UUIDs")).toStringList();
        if (!uuidListContains(uuids, ServiceUuid)) {
            continue;
        }

        BluezDevice device;
        device.path = it.key().path();
        device.address = deviceProperties.value(QStringLiteral("Address")).toString();
        device.name = deviceProperties.value(QStringLiteral("Name")).toString();
        if (device.name.isEmpty()) {
            device.name = deviceProperties.value(QStringLiteral("Alias")).toString();
        }
        device.uuids = uuids;
        device.servicesResolved = deviceProperties.value(QStringLiteral("ServicesResolved")).toBool();
        return device;
    }
    return {};
}

BleHotspotClient::BluezDevice BleHotspotClient::readBluezDevice(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }

    const auto objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it.key().path() != path) {
            continue;
        }

        const auto deviceProperties = it.value().value(QString::fromLatin1(DeviceInterface));
        BluezDevice device;
        device.path = path;
        device.address = deviceProperties.value(QStringLiteral("Address")).toString();
        device.name = deviceProperties.value(QStringLiteral("Name")).toString();
        device.uuids = deviceProperties.value(QStringLiteral("UUIDs")).toStringList();
        device.servicesResolved = deviceProperties.value(QStringLiteral("ServicesResolved")).toBool();
        return device;
    }
    return {};
}

BleHotspotClient::BluezCharacteristic BleHotspotClient::findBluezCharacteristic() const
{
    const auto objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto path = it.key().path();
        if (!m_bluezDevicePath.isEmpty() && !path.startsWith(m_bluezDevicePath + QLatin1Char('/'))) {
            continue;
        }

        const auto properties = it.value().value(QString::fromLatin1(GattCharacteristicInterface));
        if (properties.isEmpty()) {
            continue;
        }

        const auto uuid = properties.value(QStringLiteral("UUID")).toString();
        if (uuid.compare(CharacteristicUuid.toString(QUuid::WithoutBraces), Qt::CaseInsensitive) != 0) {
            continue;
        }

        BluezCharacteristic characteristic;
        characteristic.path = path;
        characteristic.flags = properties.value(QStringLiteral("Flags")).toStringList();
        return characteristic;
    }
    return {};
}

void BleHotspotClient::connectToBluezDevice()
{
    applyState(BleClientState::Connecting);
    restartOperationTimeout(bluezGattTimeoutMs(m_timeoutMs));

    QDBusInterface device(BluezService,
                          m_bluezDevicePath,
                          DeviceInterface,
                          QDBusConnection::systemBus());
    debug(tr("Connecting to EasySpot BlueZ device: path=%1 timeout=%2 ms")
              .arg(m_bluezDevicePath, QString::number(bluezGattTimeoutMs(m_timeoutMs))));
    auto *watcher = new QDBusPendingCallWatcher(device.asyncCall(QStringLiteral("Connect")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BleHotspotClient::onBluezConnectFinished);
}

void BleHotspotClient::writeBluezCommand()
{
    m_bluezPollTimer.stop();
    applyState(BleClientState::Writing);
    restartOperationTimeout(bluezGattTimeoutMs(m_timeoutMs));

    const auto characteristic = findBluezCharacteristic();
    if (characteristic.path.isEmpty()) {
        finish(false, tr("The EasySpot BLE command characteristic is missing on the Android device."));
        return;
    }

    m_bluezCharacteristicPath = characteristic.path;
    const QByteArray payload(1, m_pendingCommand == HotspotCommand::TurnOn ? '\x01' : '\x00');
    QVariantMap options;
    options.insert(QStringLiteral("type"), QStringLiteral("request"));

    debug(tr("Writing EasySpot command through BlueZ: characteristic=%1 flags=%2 payload=%3")
              .arg(characteristic.path,
                   characteristic.flags.isEmpty() ? QStringLiteral("(none)") : characteristic.flags.join(QStringLiteral(", ")),
                   QString::fromLatin1(payload.toHex(' '))));

    QDBusInterface gattCharacteristic(BluezService,
                                      characteristic.path,
                                      GattCharacteristicInterface,
                                      QDBusConnection::systemBus());
    auto *watcher = new QDBusPendingCallWatcher(gattCharacteristic.asyncCall(QStringLiteral("WriteValue"), payload, options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BleHotspotClient::onBluezWriteFinished);
}

void BleHotspotClient::stopBluezDiscovery()
{
    if (m_bluezAdapterPath.isEmpty()) {
        return;
    }

    QDBusInterface adapter(BluezService,
                           m_bluezAdapterPath,
                           AdapterInterface,
                           QDBusConnection::systemBus());
    adapter.call(QStringLiteral("StopDiscovery"));
}

void BleHotspotClient::openDiscoveredService()
{
    if (m_controller == nullptr || m_service != nullptr) {
        return;
    }

    m_service = m_controller->createServiceObject(ServiceUuid, this);
    if (m_service == nullptr) {
        finish(false, tr("The EasySpot BLE service could not be opened."));
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &BleHotspotClient::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten,
            this, &BleHotspotClient::onCharacteristicWritten);
    connect(m_service, &QLowEnergyService::errorOccurred,
            this, &BleHotspotClient::onServiceError);
    debug(tr("EasySpot service object created after service discovery completed. Discovering characteristic details."));
    applyState(BleClientState::Writing);
    restartOperationTimeout(qMax(10000, m_timeoutMs));
    m_service->discoverDetails();
}

void BleHotspotClient::connectToDiscoveredDevice(QLowEnergyController::RemoteAddressType addressType)
{
    applyState(BleClientState::Connecting);
    m_addressType = addressType;
    restartOperationTimeout(m_timeoutMs);

    debug(tr("Connecting to EasySpot device: address=%1 uuid=%2 name=%3 addressType=%4")
              .arg(m_discoveredDevice.address().toString(),
                   m_discoveredDevice.deviceUuid().toString(QUuid::WithoutBraces),
                   m_discoveredDevice.name().isEmpty() ? QStringLiteral("(unnamed)") : m_discoveredDevice.name(),
                   addressTypeText(addressType)));

    m_controller = QLowEnergyController::createCentral(m_discoveredDevice, this);
    m_controller->setRemoteAddressType(addressType);
    connect(m_controller, &QLowEnergyController::connected,
            this, &BleHotspotClient::onControllerConnected);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleHotspotClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleHotspotClient::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::stateChanged, this,
            [this](QLowEnergyController::ControllerState state) {
                debug(tr("BLE controller state changed: %1").arg(static_cast<int>(state)));
            });
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleHotspotClient::onControllerError);
    m_controller->connectToDevice();
}

void BleHotspotClient::restartOperationTimeout(int timeoutMs)
{
    m_timeoutTimer.start(timeoutMs);
    debug(tr("BLE operation timeout armed: %1 ms").arg(timeoutMs));
}

void BleHotspotClient::debug(const QString &message)
{
    const auto line = QStringLiteral("%1 %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), message);
    qInfo().noquote() << "easyspot.ble:" << line;
    emit debugMessage(line);
}
