#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "CredentialsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QIcon>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>

namespace
{
QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    return QIcon::fromTheme(name, QApplication::style()->standardIcon(fallback));
}

QIcon stateIcon(const AppState &state)
{
    if (!state.lastError.isEmpty() || state.bleState == BleClientState::Error) {
        return QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    }

    if (state.isConnectedToTarget) {
        return QIcon::fromTheme(QStringLiteral("dialog-ok-apply"),
                                QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton));
    }

    if (state.isTransitioning || state.bleState == BleClientState::Scanning ||
        state.bleState == BleClientState::Connecting || state.bleState == BleClientState::Writing ||
        state.bleState == BleClientState::Disconnecting) {
        return QIcon::fromTheme(QStringLiteral("network-transmit-receive"),
                                QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation));
    }

    return QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
}
}

MainWindow::MainWindow(HotspotWorkflowController *controller, QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_controller(controller)
{
    ui->setupUi(this);

    ui->bleTimeoutCombo->addItem(QStringLiteral("10 seconds"), 10);
    ui->bleTimeoutCombo->addItem(QStringLiteral("15 seconds"), 15);
    ui->bleTimeoutCombo->addItem(QStringLiteral("20 seconds"), 20);
    ui->bleTimeoutCombo->addItem(QStringLiteral("30 seconds"), 30);

    ui->mainTabs->setTabIcon(0, themedIcon(QStringLiteral("network-wireless-hotspot"), QStyle::SP_ComputerIcon));
    ui->mainTabs->setTabIcon(1, themedIcon(QStringLiteral("preferences-system"), QStyle::SP_FileDialogDetailedView));
    ui->mainTabs->setTabIcon(2, themedIcon(QStringLiteral("help-about"), QStyle::SP_MessageBoxInformation));

    ui->turnOnButton->setIcon(themedIcon(QStringLiteral("network-wireless-hotspot"), QStyle::SP_DialogApplyButton));
    ui->connectButton->setIcon(themedIcon(QStringLiteral("network-connect"), QStyle::SP_DriveNetIcon));
    ui->turnOffButton->setIcon(themedIcon(QStringLiteral("process-stop"), QStyle::SP_DialogCancelButton));
    ui->refreshStatusButton->setIcon(themedIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload));
    ui->editCredentialsButton->setIcon(themedIcon(QStringLiteral("document-edit"), QStyle::SP_FileDialogContentsView));
    ui->clearCredentialsButton->setIcon(themedIcon(QStringLiteral("edit-clear"), QStyle::SP_DialogDiscardButton));
    ui->bluetoothSettingsButton->setIcon(themedIcon(QStringLiteral("preferences-system-bluetooth"), QStyle::SP_ComputerIcon));
    ui->networkSettingsButton->setIcon(themedIcon(QStringLiteral("preferences-system-network"), QStyle::SP_DriveNetIcon));
    ui->copyDebugButton->setIcon(themedIcon(QStringLiteral("edit-copy"), QStyle::SP_FileDialogContentsView));
    ui->aboutIconLabel->setPixmap(QIcon(QStringLiteral(":/icons/easyspot-linux-client.png")).pixmap(48, 48));

    connect(ui->turnOffButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::turnHotspotOff);
    connect(ui->turnOnButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::turnHotspotOn);
    connect(ui->connectButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::connectToHotspot);
    connect(ui->editCredentialsButton, &QPushButton::clicked, this, &MainWindow::editCredentials);
    connect(ui->clearCredentialsButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::clearCredentials);
    connect(ui->refreshStatusButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::refreshConnectionState);
    connect(ui->launchAtLoginCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_updating) {
            m_controller->setLaunchAtLoginEnabled(checked);
        }
    });
    connect(ui->debugCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_updating) {
            m_controller->setDebugInformationEnabled(checked);
        }
    });
    connect(ui->bleTimeoutCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_updating && index >= 0) {
            m_controller->saveBleTimeout(ui->bleTimeoutCombo->itemData(index).toInt());
        }
    });
    connect(ui->bluetoothSettingsButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::openBluetoothSettings);
    connect(ui->networkSettingsButton, &QPushButton::clicked, m_controller, &HotspotWorkflowController::openNetworkSettings);
    connect(ui->copyDebugButton, &QPushButton::clicked, this, &MainWindow::copyDebugSummary);

    connect(m_controller, &HotspotWorkflowController::snapshotChanged,
            this, &MainWindow::applySnapshot);
    connect(m_controller, &HotspotWorkflowController::notificationRequested,
            this, &MainWindow::showNotificationError);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::applySnapshot(const ControllerSnapshot &snapshot)
{
    m_snapshot = snapshot;
    m_updating = true;

    ui->launchAtLoginCheck->setChecked(snapshot.settings.launchAtLoginEnabled);
    ui->debugCheck->setChecked(snapshot.settings.debugInformationEnabled);

    const auto timeoutIndex = ui->bleTimeoutCombo->findData(snapshot.settings.bleScanTimeoutSeconds);
    if (timeoutIndex >= 0) {
        ui->bleTimeoutCombo->setCurrentIndex(timeoutIndex);
    }

    ui->statusLabel->setText(statusText(snapshot.state));
    ui->detailLabel->setText(detailText(snapshot));
    ui->statusIconLabel->setPixmap(stateIcon(snapshot.state).pixmap(32, 32));
    ui->currentSsidValueLabel->setText(snapshot.state.currentSsid.isEmpty()
                                           ? tr("Not connected")
                                           : snapshot.state.currentSsid);
    ui->targetSsidValueLabel->setText(snapshot.settings.targetSsid.isEmpty()
                                          ? tr("Not set")
                                          : snapshot.settings.targetSsid);
    ui->passwordSavedValueLabel->setText(snapshot.state.isPasswordSaved ? tr("Saved in KWallet") : tr("Not saved"));
    ui->credentialSummaryLabel->setText(credentialSummary(snapshot));
    ui->credentialIconLabel->setPixmap((snapshot.state.isPasswordSaved && !snapshot.settings.targetSsid.isEmpty()
                                            ? themedIcon(QStringLiteral("dialog-password"), QStyle::SP_DialogApplyButton)
                                            : QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning))
                                           .pixmap(24, 24));
    ui->statusbar->showMessage(snapshot.state.currentSsid.isEmpty()
                                   ? tr("Current SSID: not connected")
                                   : tr("Current SSID: %1").arg(snapshot.state.currentSsid));

    ui->turnOffButton->setEnabled(m_controller->canTurnHotspotOff());
    ui->turnOnButton->setEnabled(m_controller->canTurnHotspotOn());
    ui->connectButton->setEnabled(m_controller->canConnectToHotspot());
    ui->debugGroup->setVisible(snapshot.settings.debugInformationEnabled);
    ui->debugText->setPlainText(debugSummary(snapshot));
    ui->aboutVersionLabel->setText(tr("Version: %1").arg(QCoreApplication::applicationVersion()));

    m_updating = false;
}

void MainWindow::showNotificationError(const QString &, const QString &body)
{
    if (!body.isEmpty() && window()->isActiveWindow()) {
        statusBar()->showMessage(body, 6000);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    hide();
    statusBar()->showMessage(tr("EasySpot is still running in the system tray."), 4000);
    event->ignore();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, m_controller, &HotspotWorkflowController::refreshConnectionState);
}

void MainWindow::editCredentials()
{
    CredentialsDialog dialog(this);
    dialog.setSsid(m_snapshot.settings.targetSsid);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_controller->saveCredentials(dialog.ssid(), dialog.password());
}

void MainWindow::copyDebugSummary()
{
    QApplication::clipboard()->setText(debugSummary(m_snapshot));
    statusBar()->showMessage(tr("Debug summary copied to the clipboard."), 4000);
}

QString MainWindow::statusText(const AppState &state) const
{
    if (!state.lastError.isEmpty()) {
        return tr("Action needs attention");
    }

    if (state.isConnectedToTarget) {
        return tr("Connected to hotspot");
    }

    if (state.isTransitioning && state.bleState == BleClientState::Idle) {
        return tr("Connecting to hotspot");
    }

    switch (state.bleState) {
    case BleClientState::Scanning:
        return tr("Scanning for EasySpot device");
    case BleClientState::Connecting:
        return tr("Connecting over BLE");
    case BleClientState::Writing:
        return tr("Sending BLE command");
    case BleClientState::Success:
        return tr("BLE command sent");
    case BleClientState::Disconnecting:
        return tr("Sending hotspot off command");
    case BleClientState::Error:
        return tr("Action needs attention");
    case BleClientState::Idle:
        return tr("Ready");
    }

    return tr("Ready");
}

QString MainWindow::detailText(const ControllerSnapshot &snapshot) const
{
    if (!snapshot.state.lastError.isEmpty()) {
        return snapshot.state.lastError;
    }

    if (snapshot.state.isConnectedToTarget) {
        return tr("Linux is already connected to the configured hotspot SSID.");
    }

    if (snapshot.state.isTransitioning) {
        return tr("EasySpot is processing the current request. Starting another action will interrupt it.");
    }

    if (!snapshot.state.isPasswordSaved || snapshot.settings.targetSsid.isEmpty()) {
        return tr("No hotspot SSID or password has been saved yet.");
    }

    return tr("Use Turn Hotspot OFF, Turn Hotspot ON, or Connect to Hotspot based on the Android hotspot state.");
}

QString MainWindow::credentialSummary(const ControllerSnapshot &snapshot) const
{
    if (snapshot.settings.targetSsid.isEmpty() && !snapshot.state.isPasswordSaved) {
        return tr("No hotspot SSID or password has been saved yet.");
    }

    if (snapshot.settings.targetSsid.isEmpty()) {
        return tr("A hotspot password is saved in KWallet, but no SSID is configured.");
    }

    if (!snapshot.state.isPasswordSaved) {
        return tr("SSID \"%1\" is configured, but no hotspot password is saved.").arg(snapshot.settings.targetSsid);
    }

    return tr("SSID \"%1\" is configured and the password is saved in KWallet.").arg(snapshot.settings.targetSsid);
}

QString MainWindow::debugSummary(const ControllerSnapshot &snapshot) const
{
    const auto bleLog = snapshot.state.bleDebugLog.isEmpty()
                            ? tr("(no BLE debug events yet)")
                            : snapshot.state.bleDebugLog.join(QLatin1Char('\n'));

    return tr("Target SSID: %1\nCurrent SSID: %2\nConnected to target: %3\nTransitioning: %4\nPassword saved: %5\nBLE timeout: %6 seconds\nLaunch at login: %7\n\nBLE debug log:\n%8")
        .arg(snapshot.settings.targetSsid.isEmpty() ? tr("(not set)") : snapshot.settings.targetSsid,
             snapshot.state.currentSsid.isEmpty() ? tr("(not connected)") : snapshot.state.currentSsid,
             snapshot.state.isConnectedToTarget ? tr("yes") : tr("no"),
             snapshot.state.isTransitioning ? tr("yes") : tr("no"),
             snapshot.state.isPasswordSaved ? tr("yes") : tr("no"),
             QString::number(snapshot.settings.bleScanTimeoutSeconds),
             snapshot.settings.launchAtLoginEnabled ? tr("yes") : tr("no"),
             bleLog);
}
