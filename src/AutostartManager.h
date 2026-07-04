#pragma once

#include <QObject>
#include <QString>

class AutostartManager final : public QObject
{
    Q_OBJECT

public:
    explicit AutostartManager(QObject *parent = nullptr);

    bool isEnabled() const;
    bool setEnabled(bool enabled, QString *errorMessage = nullptr);

private:
    static QString autostartFilePath();
    static QString executablePath();
};
