#include "CredentialsDialog.h"
#include "ui_CredentialsDialog.h"

CredentialsDialog::CredentialsDialog(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::CredentialsDialog)
{
    ui->setupUi(this);
}

CredentialsDialog::~CredentialsDialog()
{
    delete ui;
}

void CredentialsDialog::setSsid(const QString &ssid)
{
    ui->ssidEdit->setText(ssid);
}

QString CredentialsDialog::ssid() const
{
    return ui->ssidEdit->text();
}

QString CredentialsDialog::password() const
{
    return ui->passwordEdit->text();
}
