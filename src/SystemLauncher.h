#pragma once

#include <QObject>

class SystemLauncher final : public QObject
{
    Q_OBJECT

public:
    explicit SystemLauncher(QObject *parent = nullptr);

public slots:
    void openBluetoothSettings();
    void openNetworkSettings();
};
