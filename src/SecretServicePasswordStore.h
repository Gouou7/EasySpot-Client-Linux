#pragma once

#include <QObject>
#include <QString>

class PasswordStore : public QObject
{
    Q_OBJECT

public:
    explicit PasswordStore(QObject *parent = nullptr) : QObject(parent) {}
    virtual bool hasPassword() = 0;
    virtual QString loadPassword() = 0;
    virtual bool savePassword(const QString &password, QString *errorMessage = nullptr) = 0;
    virtual bool clearPassword(QString *errorMessage = nullptr) = 0;
};

class SecretServicePasswordStore final : public PasswordStore
{
    Q_OBJECT

public:
    explicit SecretServicePasswordStore(QObject *parent = nullptr);

    bool hasPassword() override;
    QString loadPassword() override;
    bool savePassword(const QString &password, QString *errorMessage = nullptr) override;
    bool clearPassword(QString *errorMessage = nullptr) override;

private:
    static QString serviceName();
    static QString schemaName();
};
