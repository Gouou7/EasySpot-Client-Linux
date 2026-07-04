#include "AutostartManager.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class AutostartManagerTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_configDir.isValid());
        qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
    }

    void setEnabledWritesTrayAutostartDesktopFile()
    {
        AutostartManager manager;

        QString errorMessage;
        QVERIFY2(manager.setEnabled(true, &errorMessage), qPrintable(errorMessage));
        QVERIFY(manager.isEnabled());

        QFile file(m_configDir.filePath(QStringLiteral("autostart/easyspot-linux-client.desktop")));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto content = QString::fromUtf8(file.readAll());

        QVERIFY(content.contains(QStringLiteral("Type=Application\n")));
        QVERIFY(content.contains(QStringLiteral("Exec=\"")));
        QVERIFY(content.contains(QStringLiteral("\" --tray\n")));
        QVERIFY(content.contains(QStringLiteral("Icon=easyspot-linux-client\n")));
        QVERIFY(content.contains(QStringLiteral("Hidden=false\n")));
        QVERIFY(content.contains(QStringLiteral("X-KDE-autostart-after=panel\n")));
        QVERIFY(content.contains(QStringLiteral("OnlyShowIn=KDE;GNOME;XFCE;LXQt;\n")));

        QVERIFY2(manager.setEnabled(false, &errorMessage), qPrintable(errorMessage));
        QVERIFY(!manager.isEnabled());
    }

private:
    QTemporaryDir m_configDir;
};

QTEST_GUILESS_MAIN(AutostartManagerTests)
#include "AutostartManagerTests.moc"
