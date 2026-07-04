#pragma once

#include "AppTypes.h"
#include "AutostartManager.h"
#include "BleHotspotClient.h"
#include "NetworkManagerWifiController.h"
#include "SecretServicePasswordStore.h"
#include "SettingsStore.h"
#include "SystemLauncher.h"

#include <QObject>
#include <QTimer>

class HotspotWorkflowController final : public QObject
{
    Q_OBJECT

public:
    explicit HotspotWorkflowController(HotspotBleClient *bleClient,
                                       WifiController *wifiController,
                                       PasswordStore *passwordStore,
                                       SettingsStore *settingsStore,
                                       AutostartManager *autostartManager,
                                       SystemLauncher *systemLauncher,
                                       QObject *parent = nullptr);

    ControllerSnapshot snapshot() const;
    bool canTurnHotspotOn() const;
    bool canConnectToHotspot() const;
    bool canTurnHotspotOff() const;

    void setTimingForTests(int turnOnDelayMs, int turnOnTimeoutMs, int connectOnlyTimeoutMs, int retryDelayMs);

public slots:
    void initialize();
    void turnHotspotOn();
    void turnHotspotOff();
    void connectToHotspot();
    void refreshConnectionState();
    void saveCredentials(const QString &ssid, const QString &password);
    void clearCredentials();
    void saveBleTimeout(int seconds);
    void setLaunchAtLoginEnabled(bool enabled);
    void setDebugInformationEnabled(bool enabled);
    void openBluetoothSettings();
    void openNetworkSettings();

signals:
    void snapshotChanged(const ControllerSnapshot &snapshot);
    void notificationRequested(const QString &title, const QString &body);

private slots:
    void onBleStateChanged(BleClientState state);
    void onBleDebugMessage(const QString &message);
    void onBleCommandFinished(bool success, const QString &errorMessage);
    void onWifiConnectFinished(bool success, const QString &errorMessage);
    void onCurrentSsidChanged(const QString &ssid);
    void onTransitionTimeout();

private:
    enum class Operation
    {
        None,
        TurnOn,
        TurnOff,
        ConnectOnly,
    };

    void applyState(const AppState &state);
    void notify();
    void beginTransition(Operation operation, int timeoutMs);
    void endTransition();
    void cancelTransition();
    void scheduleWifiAttempt(int delayMs = 0);
    void startWifiAttempt();
    void finishWithError(const QString &message);
    bool isConnectedToTarget(const QString &ssid) const;
    QString requireTargetSsid() const;
    QString requirePassword() const;

    HotspotBleClient *m_bleClient;
    WifiController *m_wifiController;
    PasswordStore *m_passwordStore;
    SettingsStore *m_settingsStore;
    AutostartManager *m_autostartManager;
    SystemLauncher *m_systemLauncher;

    AppSettings m_settings;
    AppState m_state;
    Operation m_operation = Operation::None;
    int m_operationId = 0;
    int m_turnOnDelayMs = 5000;
    int m_turnOnTimeoutMs = 60000;
    int m_connectOnlyTimeoutMs = 15000;
    int m_retryDelayMs = 5000;
    qint64 m_transitionDeadline = 0;
    QTimer m_transitionTimer;
    QTimer m_wifiRetryTimer;
};
