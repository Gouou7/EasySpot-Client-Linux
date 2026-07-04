#pragma once

#include "HotspotWorkflowController.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(HotspotWorkflowController *controller, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void applySnapshot(const ControllerSnapshot &snapshot);
    void showNotificationError(const QString &title, const QString &body);

private:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void editCredentials();
    void copyDebugSummary();
    QString statusText(const AppState &state) const;
    QString detailText(const ControllerSnapshot &snapshot) const;
    QString credentialSummary(const ControllerSnapshot &snapshot) const;
    QString debugSummary(const ControllerSnapshot &snapshot) const;

    Ui::MainWindow *ui;
    HotspotWorkflowController *m_controller;
    ControllerSnapshot m_snapshot;
    bool m_updating = false;
};
