#include "TrayController.h"

#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

TrayController::TrayController(HotspotWorkflowController *controller, MainWindow *window, QObject *parent)
    : QObject(parent),
      m_controller(controller),
      m_window(window)
{
    m_statusAction = m_menu.addAction(tr("Status: Idle"));
    m_statusAction->setEnabled(false);
    m_menu.addSeparator();
    m_turnOffAction = m_menu.addAction(tr("Turn Hotspot OFF"), m_controller, &HotspotWorkflowController::turnHotspotOff);
    m_turnOnAction = m_menu.addAction(tr("Turn Hotspot ON"), m_controller, &HotspotWorkflowController::turnHotspotOn);
    m_connectAction = m_menu.addAction(tr("Connect to Hotspot"), m_controller, &HotspotWorkflowController::connectToHotspot);
    m_menu.addSeparator();
    m_menu.addAction(tr("Open Settings"), m_window, [this]() {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    m_toggleStartupAction = m_menu.addAction(tr("Enable Launch at Login"));
    connect(m_toggleStartupAction, &QAction::triggered, this, [this]() {
        const auto snapshot = m_controller->snapshot();
        m_controller->setLaunchAtLoginEnabled(!snapshot.settings.launchAtLoginEnabled);
    });
    m_menu.addSeparator();
    m_menu.addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    connect(&m_menu, &QMenu::aboutToShow, m_controller, &HotspotWorkflowController::refreshConnectionState);

    m_trayIcon.setContextMenu(&m_menu);
    m_trayIcon.setToolTip(tr("EasySpot for Linux"));
    m_trayIcon.setIcon(QIcon(QStringLiteral(":/icons/easyspot-linux-client.png")));

    connect(&m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_window->setVisible(!m_window->isVisible());
        }
    });
    connect(m_controller, &HotspotWorkflowController::snapshotChanged,
            this, &TrayController::applySnapshot);
    connect(m_controller, &HotspotWorkflowController::notificationRequested,
            this, &TrayController::showMessage);
}

void TrayController::show()
{
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon.show();
    }
}

void TrayController::applySnapshot(const ControllerSnapshot &snapshot)
{
    QString status = tr("Idle");
    if (!snapshot.state.lastError.isEmpty()) {
        status = tr("Needs attention");
    } else if (snapshot.state.isConnectedToTarget) {
        status = tr("Connected to hotspot");
    } else if (snapshot.state.isTransitioning) {
        status = tr("Working");
    }

    m_statusAction->setText(tr("Status: %1").arg(status));
    m_turnOffAction->setEnabled(m_controller->canTurnHotspotOff());
    m_turnOnAction->setEnabled(m_controller->canTurnHotspotOn());
    m_connectAction->setEnabled(m_controller->canConnectToHotspot());
    m_toggleStartupAction->setText(snapshot.settings.launchAtLoginEnabled
                                       ? tr("Disable Launch at Login")
                                       : tr("Enable Launch at Login"));
    m_trayIcon.setToolTip(tr("EasySpot for Linux - %1").arg(status));
}

void TrayController::showMessage(const QString &title, const QString &body)
{
    if (m_trayIcon.isVisible()) {
        m_trayIcon.showMessage(title, body, QSystemTrayIcon::Information, 5000);
    }
}
