#include "SecretServicePasswordStore.h"

#include <QDBusMetaType>
#include <QMap>
#include <QMetaType>
#include <QTest>

class SecretServicePasswordStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void constructorRegistersAttributeMapForDbus()
    {
        SecretServicePasswordStore store;

        const char *signature = QDBusMetaType::typeToSignature(QMetaType::fromType<QMap<QString, QString>>());
        QVERIFY(signature != nullptr);
        QCOMPARE(QByteArray(signature), QByteArray("a{ss}"));
    }
};

QTEST_GUILESS_MAIN(SecretServicePasswordStoreTests)
#include "SecretServicePasswordStoreTests.moc"
