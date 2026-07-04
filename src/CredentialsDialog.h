#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class CredentialsDialog; }
QT_END_NAMESPACE

class CredentialsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit CredentialsDialog(QWidget *parent = nullptr);
    ~CredentialsDialog() override;

    void setSsid(const QString &ssid);
    QString ssid() const;
    QString password() const;

private:
    Ui::CredentialsDialog *ui;
};
