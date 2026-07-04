#pragma once

#include "HotspotWorkflowController.h"

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class MainWindow;

class TrayController final : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(HotspotWorkflowController *controller, MainWindow *window, QObject *parent = nullptr);
    void show();

private slots:
    void applySnapshot(const ControllerSnapshot &snapshot);
    void showMessage(const QString &title, const QString &body);

private:
    HotspotWorkflowController *m_controller;
    MainWindow *m_window;
    QSystemTrayIcon m_trayIcon;
    QMenu m_menu;
    QAction *m_statusAction = nullptr;
    QAction *m_turnOffAction = nullptr;
    QAction *m_turnOnAction = nullptr;
    QAction *m_connectAction = nullptr;
    QAction *m_toggleStartupAction = nullptr;
};
