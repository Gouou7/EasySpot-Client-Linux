#include "HotspotWorkflowController.h"

#include <QTemporaryDir>
#include <QTest>

class FakeBleClient final : public HotspotBleClient
{
    Q_OBJECT

public:
    QList<HotspotCommand> requests;
    bool autoComplete = true;

    void trigger(HotspotCommand command, int) override
    {
        requests.append(command);
        emit stateChanged(command == HotspotCommand::TurnOn ? BleClientState::Writing : BleClientState::Disconnecting);
        if (autoComplete) {
            emit commandFinished(true, {});
        }
    }
};

class FakeWifiController final : public WifiController
{
    Q_OBJECT

public:
    QString ssid;
    QList<QString> connectRequests;
    bool connectSucceeds = true;

    QString currentSsid() const override { return ssid; }
    void startMonitoring() override {}
    void stopMonitoring() override {}

    void connectToTarget(const QString &targetSsid, const QString &, int) override
    {
        connectRequests.append(targetSsid);
        if (connectSucceeds) {
            ssid = targetSsid;
            emit currentSsidChanged(ssid);
            emit connectFinished(true, {});
        } else {
            emit connectFinished(false, QStringLiteral("not visible"));
        }
    }
};

class FakePasswordStore final : public PasswordStore
{
    Q_OBJECT

public:
    QString password;
    int loadPasswordCalls = 0;

    bool hasPassword() override { return !password.isEmpty(); }
    QString loadPassword() override
    {
        ++loadPasswordCalls;
        return password;
    }
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

class HotspotWorkflowControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
        qRegisterMetaType<ControllerSnapshot>("ControllerSnapshot");
    }

    void turnHotspotOnSendsBleThenConnectsAfterDelay()
    {
        Fixture fixture;
        fixture.controller.setTimingForTests(25, 500, 150, 10);
        fixture.controller.initialize();
        fixture.controller.saveCredentials(QStringLiteral("PhoneHotspot"), QStringLiteral("secret"));

        fixture.controller.turnHotspotOn();

        QCOMPARE(fixture.ble.requests.size(), 1);
        QVERIFY(fixture.ble.requests.first() == HotspotCommand::TurnOn);
        QCOMPARE(fixture.wifi.connectRequests.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.wifi.connectRequests.size(), 1, 200);
        QCOMPARE(fixture.wifi.connectRequests.first(), QStringLiteral("PhoneHotspot"));
    }

    void connectToHotspotDoesNotSendBle()
    {
        Fixture fixture;
        fixture.controller.setTimingForTests(5, 500, 150, 10);
        fixture.controller.initialize();
        fixture.controller.saveCredentials(QStringLiteral("PhoneHotspot"), QStringLiteral("secret"));

        fixture.controller.connectToHotspot();

        QTRY_COMPARE_WITH_TIMEOUT(fixture.wifi.connectRequests.size(), 1, 100);
        QVERIFY(fixture.ble.requests.isEmpty());
    }

    void turnHotspotOffSendsBleEvenWhenNotConnected()
    {
        Fixture fixture;
        fixture.controller.initialize();

        fixture.controller.turnHotspotOff();

        QCOMPARE(fixture.ble.requests.size(), 1);
        QVERIFY(fixture.ble.requests.first() == HotspotCommand::TurnOff);
    }

    void missingCredentialsReportError()
    {
        Fixture fixture;
        fixture.controller.initialize();

        fixture.controller.turnHotspotOn();

        QVERIFY(fixture.ble.requests.isEmpty());
        QVERIFY(!fixture.controller.snapshot().state.lastError.isEmpty());
    }

    void alreadyConnectedDisablesTurnOnAndConnect()
    {
        Fixture fixture;
        fixture.wifi.ssid = QStringLiteral("PhoneHotspot");
        fixture.controller.initialize();
        fixture.controller.saveCredentials(QStringLiteral("PhoneHotspot"), QStringLiteral("secret"));

        QVERIFY(!fixture.controller.canTurnHotspotOn());
        QVERIFY(!fixture.controller.canConnectToHotspot());
        QVERIFY(fixture.controller.canTurnHotspotOff());
    }

    void newOperationPreemptsPendingTurnOn()
    {
        Fixture fixture;
        fixture.ble.autoComplete = false;
        fixture.controller.setTimingForTests(100, 500, 150, 10);
        fixture.controller.initialize();
        fixture.controller.saveCredentials(QStringLiteral("PhoneHotspot"), QStringLiteral("secret"));

        fixture.controller.turnHotspotOn();
        fixture.controller.connectToHotspot();

        QCOMPARE(fixture.ble.requests.size(), 1);
        QVERIFY(fixture.ble.requests.first() == HotspotCommand::TurnOn);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.wifi.connectRequests.size(), 1, 100);
    }

    void saveCredentialsStoresSsidOutsidePasswordStore()
    {
        Fixture fixture;
        fixture.controller.initialize();

        fixture.controller.saveCredentials(QStringLiteral(" PhoneHotspot "), QStringLiteral("secret"));

        QCOMPARE(fixture.settingsStore.load().targetSsid, QStringLiteral("PhoneHotspot"));
        QCOMPARE(fixture.passwordStore.password, QStringLiteral("secret"));
    }

    void initializeChecksPasswordPresenceWithoutLoadingSecret()
    {
        Fixture fixture;
        fixture.passwordStore.password = QStringLiteral("secret");

        fixture.controller.initialize();

        QVERIFY(fixture.controller.snapshot().state.isPasswordSaved);
        QCOMPARE(fixture.passwordStore.loadPasswordCalls, 0);
    }

private:
    struct Fixture
    {
        FakeBleClient ble;
        FakeWifiController wifi;
        FakePasswordStore passwordStore;
        SettingsStore settingsStore;
        AutostartManager autostartManager;
        SystemLauncher launcher;
        HotspotWorkflowController controller;

        Fixture()
            : controller(&ble, &wifi, &passwordStore, &settingsStore, &autostartManager, &launcher)
        {
            settingsStore.saveTargetSsid({});
            settingsStore.saveBleScanTimeoutSeconds(15);
            settingsStore.saveLaunchAtLoginEnabled(false);
            settingsStore.saveDebugInformationEnabled(false);
        }
    };

    QTemporaryDir m_configDir;
};

QTEST_GUILESS_MAIN(HotspotWorkflowControllerTests)
#include "HotspotWorkflowControllerTests.moc"
