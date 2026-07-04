#include "HotspotWorkflowController.h"
#include "MainWindow.h"

#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

class UiFakeBleClient final : public HotspotBleClient
{
    Q_OBJECT

public:
    QList<HotspotCommand> requests;

    void trigger(HotspotCommand command, int) override
    {
        requests.append(command);
        emit stateChanged(BleClientState::Writing);
        emit commandFinished(true, {});
    }
};

class UiFakeWifiController final : public WifiController
{
    Q_OBJECT

public:
    QString ssid;

    QString currentSsid() const override { return ssid; }
    void startMonitoring() override {}
    void stopMonitoring() override {}
    void connectToTarget(const QString &, const QString &, int) override {}
};

class UiFakePasswordStore final : public PasswordStore
{
    Q_OBJECT

public:
    QString password;

    bool hasPassword() override { return !password.isEmpty(); }
    QString loadPassword() override { return password; }
    bool savePassword(const QString &value, QString *) override
    {
        password = value;
        return true;
    }
    bool clearPassword(QString *) override
    {
        password.clear();
        return true;
    }
};

class MainWindowTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
        qRegisterMetaType<ControllerSnapshot>("ControllerSnapshot");
    }

    void turnOnButtonTriggersBleTurnOn()
    {
        UiFakeBleClient ble;
        UiFakeWifiController wifi;
        UiFakePasswordStore passwordStore;
        SettingsStore settingsStore;
        AutostartManager autostartManager;
        SystemLauncher launcher;
        HotspotWorkflowController controller(&ble, &wifi, &passwordStore, &settingsStore, &autostartManager, &launcher);
        MainWindow window(&controller);

        settingsStore.saveTargetSsid({});
        settingsStore.saveBleScanTimeoutSeconds(15);
        settingsStore.saveLaunchAtLoginEnabled(false);
        settingsStore.saveDebugInformationEnabled(false);
        controller.initialize();
        controller.saveCredentials(QStringLiteral("PhoneHotspot"), QStringLiteral("secret"));

        auto *turnOnButton = window.findChild<QPushButton *>(QStringLiteral("turnOnButton"));
        QVERIFY(turnOnButton != nullptr);
        QVERIFY(turnOnButton->isEnabled());

        turnOnButton->click();

        QCOMPARE(ble.requests.size(), 1);
        QCOMPARE(ble.requests.first(), HotspotCommand::TurnOn);
    }

private:
    QTemporaryDir m_configDir;
};

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
