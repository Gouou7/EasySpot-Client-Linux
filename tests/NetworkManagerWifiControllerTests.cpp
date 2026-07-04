#include "NetworkManagerWifiController.h"

#include <QDBusMetaType>
#include <QTest>

class NetworkManagerWifiControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void volatileActivationOptionsUseNetworkManagerMemoryOnlyProfile()
    {
        const auto options = NetworkManagerWifiController::buildVolatileActivationOptions();

        QCOMPARE(options.value(QStringLiteral("persist")).toString(), QStringLiteral("volatile"));
    }

    void volatileConnectionSettingsIncludeWifiSecuritySecretOnlyForActivation()
    {
        const auto settings = NetworkManagerWifiController::buildVolatileConnectionSettings(QStringLiteral("PhoneHotspot"),
                                                                                            QStringLiteral("secret"));

        const auto connection = settings.value(QStringLiteral("connection")).toMap();
        QCOMPARE(connection.value(QStringLiteral("id")).toString(), QStringLiteral("EasySpot PhoneHotspot"));
        QCOMPARE(connection.value(QStringLiteral("type")).toString(), QStringLiteral("802-11-wireless"));
        QCOMPARE(connection.value(QStringLiteral("autoconnect")).toBool(), false);

        const auto wireless = settings.value(QStringLiteral("802-11-wireless")).toMap();
        QCOMPARE(QString::fromUtf8(wireless.value(QStringLiteral("ssid")).toByteArray()), QStringLiteral("PhoneHotspot"));
        QCOMPARE(wireless.value(QStringLiteral("mode")).toString(), QStringLiteral("infrastructure"));
        QCOMPARE(wireless.value(QStringLiteral("security")).toString(), QStringLiteral("802-11-wireless-security"));

        const auto security = settings.value(QStringLiteral("802-11-wireless-security")).toMap();
        QCOMPARE(security.value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("wpa-psk"));
        QCOMPARE(security.value(QStringLiteral("psk")).toString(), QStringLiteral("secret"));
    }

    void volatileConnectionSettingsUseNetworkManagerDbusSignature()
    {
        qDBusRegisterMetaType<NetworkManagerConnectionSettings>();

        const char *signature = QDBusMetaType::typeToSignature(QMetaType::fromType<NetworkManagerConnectionSettings>());
        QVERIFY(signature != nullptr);
        QCOMPARE(QByteArray(signature), QByteArray("a{sa{sv}}"));
    }
};

QTEST_GUILESS_MAIN(NetworkManagerWifiControllerTests)
#include "NetworkManagerWifiControllerTests.moc"
