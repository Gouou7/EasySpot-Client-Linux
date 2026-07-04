#include "HotspotWorkflowController.h"

#include <QDateTime>

HotspotWorkflowController::HotspotWorkflowController(HotspotBleClient *bleClient,
                                                     WifiController *wifiController,
                                                     PasswordStore *passwordStore,
                                                     SettingsStore *settingsStore,
                                                     AutostartManager *autostartManager,
                                                     SystemLauncher *systemLauncher,
                                                     QObject *parent)
    : QObject(parent),
      m_bleClient(bleClient),
      m_wifiController(wifiController),
      m_passwordStore(passwordStore),
      m_settingsStore(settingsStore),
      m_autostartManager(autostartManager),
      m_systemLauncher(systemLauncher)
{
    m_transitionTimer.setSingleShot(true);
    m_wifiRetryTimer.setSingleShot(true);
    connect(&m_transitionTimer, &QTimer::timeout, this, &HotspotWorkflowController::onTransitionTimeout);
    connect(&m_wifiRetryTimer, &QTimer::timeout, this, &HotspotWorkflowController::startWifiAttempt);

    connect(m_bleClient, &HotspotBleClient::stateChanged,
            this, &HotspotWorkflowController::onBleStateChanged);
    connect(m_bleClient, &HotspotBleClient::debugMessage,
            this, &HotspotWorkflowController::onBleDebugMessage);
    connect(m_bleClient, &HotspotBleClient::commandFinished,
            this, &HotspotWorkflowController::onBleCommandFinished);
    connect(m_wifiController, &WifiController::currentSsidChanged,
            this, &HotspotWorkflowController::onCurrentSsidChanged);
    connect(m_wifiController, &WifiController::connectFinished,
            this, &HotspotWorkflowController::onWifiConnectFinished);
}

ControllerSnapshot HotspotWorkflowController::snapshot() const
{
    return ControllerSnapshot{m_settings, m_state};
}

bool HotspotWorkflowController::canTurnHotspotOn() const
{
    return !m_state.isTransitioning && !m_state.isConnectedToTarget;
}

bool HotspotWorkflowController::canConnectToHotspot() const
{
    return !m_state.isTransitioning && !m_state.isConnectedToTarget;
}

bool HotspotWorkflowController::canTurnHotspotOff() const
{
    return !m_state.isTransitioning;
}

void HotspotWorkflowController::setTimingForTests(int turnOnDelayMs, int turnOnTimeoutMs, int connectOnlyTimeoutMs, int retryDelayMs)
{
    m_turnOnDelayMs = turnOnDelayMs;
    m_turnOnTimeoutMs = turnOnTimeoutMs;
    m_connectOnlyTimeoutMs = connectOnlyTimeoutMs;
    m_retryDelayMs = retryDelayMs;
}

void HotspotWorkflowController::initialize()
{
    m_settings = m_settingsStore->load();
    m_settings.launchAtLoginEnabled = m_autostartManager->isEnabled();
    if (m_settings.launchAtLoginEnabled) {
        QString ignoredErrorMessage;
        m_autostartManager->setEnabled(true, &ignoredErrorMessage);
    }
    m_state.currentSsid = m_wifiController->currentSsid();
    m_state.isConnectedToTarget = isConnectedToTarget(m_state.currentSsid);
    m_state.isPasswordSaved = m_passwordStore->hasPassword();
    m_wifiController->startMonitoring();
    notify();
}

void HotspotWorkflowController::turnHotspotOn()
{
    cancelTransition();

    const auto targetSsid = requireTargetSsid();
    const auto password = requirePassword();
    if (targetSsid.isEmpty() || password.isEmpty()) {
        return;
    }

    Q_UNUSED(password);
    beginTransition(Operation::TurnOn, m_turnOnTimeoutMs);
    applyState(AppState{BleClientState::Scanning, m_state.currentSsid, m_state.isConnectedToTarget, true, {}, m_state.isPasswordSaved});
    m_bleClient->trigger(HotspotCommand::TurnOn, m_settings.bleScanTimeoutSeconds * 1000);
    scheduleWifiAttempt(m_turnOnDelayMs);
}

void HotspotWorkflowController::turnHotspotOff()
{
    cancelTransition();
    beginTransition(Operation::TurnOff, m_settings.bleScanTimeoutSeconds * 1000);
    applyState(AppState{BleClientState::Disconnecting, m_state.currentSsid, m_state.isConnectedToTarget, true, {}, m_state.isPasswordSaved});
    m_bleClient->trigger(HotspotCommand::TurnOff, m_settings.bleScanTimeoutSeconds * 1000);
}

void HotspotWorkflowController::connectToHotspot()
{
    cancelTransition();

    const auto targetSsid = requireTargetSsid();
    const auto password = requirePassword();
    if (targetSsid.isEmpty() || password.isEmpty()) {
        return;
    }

    Q_UNUSED(targetSsid);
    Q_UNUSED(password);
    beginTransition(Operation::ConnectOnly, m_connectOnlyTimeoutMs);
    applyState(AppState{BleClientState::Idle, m_state.currentSsid, m_state.isConnectedToTarget, true, {}, m_state.isPasswordSaved});
    scheduleWifiAttempt();
}

void HotspotWorkflowController::refreshConnectionState()
{
    const auto currentSsid = m_wifiController->currentSsid();
    applyState(AppState{m_state.bleState,
                        currentSsid,
                        isConnectedToTarget(currentSsid),
                        m_state.isTransitioning,
                        isConnectedToTarget(currentSsid) ? QString() : m_state.lastError,
                        m_passwordStore->hasPassword()});
}

void HotspotWorkflowController::saveCredentials(const QString &ssid, const QString &password)
{
    if (ssid.trimmed().isEmpty() || password.trimmed().isEmpty()) {
        finishWithError(tr("Enter both the hotspot SSID and password before saving."));
        return;
    }

    QString errorMessage;
    if (!m_passwordStore->savePassword(password, &errorMessage)) {
        finishWithError(errorMessage);
        return;
    }

    m_settings.targetSsid = ssid.trimmed();
    m_settingsStore->saveTargetSsid(m_settings.targetSsid);
    applyState(AppState{m_state.bleState, m_state.currentSsid, isConnectedToTarget(m_state.currentSsid), m_state.isTransitioning, {}, true});
}

void HotspotWorkflowController::clearCredentials()
{
    QString errorMessage;
    m_passwordStore->clearPassword(&errorMessage);
    m_settings.targetSsid.clear();
    m_settingsStore->saveTargetSsid({});
    applyState(AppState{m_state.bleState, m_state.currentSsid, false, m_state.isTransitioning, {}, false});
}

void HotspotWorkflowController::saveBleTimeout(int seconds)
{
    m_settings.bleScanTimeoutSeconds = seconds;
    m_settingsStore->saveBleScanTimeoutSeconds(seconds);
    notify();
}

void HotspotWorkflowController::setLaunchAtLoginEnabled(bool enabled)
{
    QString errorMessage;
    if (!m_autostartManager->setEnabled(enabled, &errorMessage)) {
        finishWithError(errorMessage);
        return;
    }

    m_settings.launchAtLoginEnabled = enabled;
    m_settingsStore->saveLaunchAtLoginEnabled(enabled);
    notify();
}

void HotspotWorkflowController::setDebugInformationEnabled(bool enabled)
{
    m_settings.debugInformationEnabled = enabled;
    m_settingsStore->saveDebugInformationEnabled(enabled);
    notify();
}

void HotspotWorkflowController::openBluetoothSettings()
{
    m_systemLauncher->openBluetoothSettings();
}

void HotspotWorkflowController::openNetworkSettings()
{
    m_systemLauncher->openNetworkSettings();
}

void HotspotWorkflowController::onBleStateChanged(BleClientState state)
{
    applyState(AppState{state, m_state.currentSsid, m_state.isConnectedToTarget, m_state.isTransitioning, m_state.lastError, m_state.isPasswordSaved});
}

void HotspotWorkflowController::onBleDebugMessage(const QString &message)
{
    auto state = m_state;
    state.bleDebugLog.append(message);
    constexpr int MaxBleDebugLines = 80;
    while (state.bleDebugLog.size() > MaxBleDebugLines) {
        state.bleDebugLog.removeFirst();
    }
    applyState(state);
}

void HotspotWorkflowController::onBleCommandFinished(bool success, const QString &errorMessage)
{
    if (m_operation == Operation::None) {
        return;
    }

    if (!success) {
        finishWithError(errorMessage);
        return;
    }

    if (m_operation == Operation::TurnOff) {
        emit notificationRequested(QStringLiteral("EasySpot"), tr("Hotspot off command sent."));
        applyState(AppState{BleClientState::Idle, m_state.currentSsid, false, false, {}, m_state.isPasswordSaved});
        endTransition();
        return;
    }

    if (m_operation == Operation::TurnOn) {
        emit notificationRequested(QStringLiteral("EasySpot"), tr("Hotspot command sent. Waiting for Linux to join the hotspot."));
    }
}

void HotspotWorkflowController::onWifiConnectFinished(bool success, const QString &errorMessage)
{
    if (m_operation != Operation::TurnOn && m_operation != Operation::ConnectOnly) {
        return;
    }

    if (success || isConnectedToTarget(m_wifiController->currentSsid())) {
        emit notificationRequested(QStringLiteral("EasySpot"), tr("Linux connected to the hotspot."));
        applyState(AppState{BleClientState::Idle, m_wifiController->currentSsid(), true, false, {}, m_state.isPasswordSaved});
        endTransition();
        return;
    }

    if (QDateTime::currentMSecsSinceEpoch() < m_transitionDeadline) {
        Q_UNUSED(errorMessage);
        scheduleWifiAttempt(m_retryDelayMs);
        return;
    }

    finishWithError(errorMessage.isEmpty() ? tr("Linux could not join the hotspot before the timeout.") : errorMessage);
}

void HotspotWorkflowController::onCurrentSsidChanged(const QString &ssid)
{
    const auto connected = isConnectedToTarget(ssid);
    applyState(AppState{connected ? BleClientState::Idle : m_state.bleState,
                        ssid,
                        connected,
                        connected ? false : m_state.isTransitioning,
                        connected ? QString() : m_state.lastError,
                        m_state.isPasswordSaved});
    if (connected) {
        endTransition();
    }
}

void HotspotWorkflowController::onTransitionTimeout()
{
    finishWithError(m_operation == Operation::TurnOn
                        ? tr("The hotspot command was sent, but Linux could not join the hotspot within 60 seconds.")
                        : tr("Linux could not complete the EasySpot action before the timeout."));
}

void HotspotWorkflowController::applyState(const AppState &state)
{
    const auto previousBleDebugLog = m_state.bleDebugLog;
    m_state = state;
    if (m_state.bleDebugLog.isEmpty()) {
        m_state.bleDebugLog = previousBleDebugLog;
    }
    notify();
}

void HotspotWorkflowController::notify()
{
    emit snapshotChanged(snapshot());
}

void HotspotWorkflowController::beginTransition(Operation operation, int timeoutMs)
{
    ++m_operationId;
    m_operation = operation;
    m_state.bleDebugLog.clear();
    m_transitionDeadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    m_transitionTimer.start(timeoutMs);
}

void HotspotWorkflowController::endTransition()
{
    m_transitionTimer.stop();
    m_wifiRetryTimer.stop();
    m_operation = Operation::None;
}

void HotspotWorkflowController::cancelTransition()
{
    if (m_operation == Operation::None) {
        return;
    }

    ++m_operationId;
    endTransition();
    applyState(AppState{BleClientState::Idle, m_state.currentSsid, m_state.isConnectedToTarget, false, {}, m_state.isPasswordSaved});
}

void HotspotWorkflowController::scheduleWifiAttempt(int delayMs)
{
    const auto operationId = m_operationId;
    m_wifiRetryTimer.stop();
    QTimer::singleShot(delayMs, this, [this, operationId]() {
        if (operationId == m_operationId) {
            startWifiAttempt();
        }
    });
}

void HotspotWorkflowController::startWifiAttempt()
{
    if (m_operation != Operation::TurnOn && m_operation != Operation::ConnectOnly) {
        return;
    }

    if (isConnectedToTarget(m_wifiController->currentSsid())) {
        onWifiConnectFinished(true, {});
        return;
    }

    const auto remainingMs = static_cast<int>(qMax<qint64>(1000, m_transitionDeadline - QDateTime::currentMSecsSinceEpoch()));
    m_wifiController->connectToTarget(m_settings.targetSsid, m_passwordStore->loadPassword(), qMin(remainingMs, 10000));
}

void HotspotWorkflowController::finishWithError(const QString &message)
{
    endTransition();
    applyState(AppState{BleClientState::Error, m_state.currentSsid, m_state.isConnectedToTarget, false, message, m_state.isPasswordSaved});
    if (!message.isEmpty()) {
        emit notificationRequested(QStringLiteral("EasySpot"), message);
    }
}

bool HotspotWorkflowController::isConnectedToTarget(const QString &ssid) const
{
    return !m_settings.targetSsid.isEmpty() && ssid == m_settings.targetSsid;
}

QString HotspotWorkflowController::requireTargetSsid() const
{
    if (m_settings.targetSsid.trimmed().isEmpty()) {
        const_cast<HotspotWorkflowController *>(this)->finishWithError(tr("Save the hotspot SSID before using this action."));
        return {};
    }

    return m_settings.targetSsid;
}

QString HotspotWorkflowController::requirePassword() const
{
    const auto password = m_passwordStore->loadPassword();
    if (password.trimmed().isEmpty()) {
        const_cast<HotspotWorkflowController *>(this)->finishWithError(tr("Save the hotspot Wi-Fi password before using this action."));
        return {};
    }

    return password;
}
