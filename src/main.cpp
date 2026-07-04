#include "AutostartManager.h"
#include "BleHotspotClient.h"
#include "HotspotWorkflowController.h"
#include "MainWindow.h"
#include "NetworkManagerWifiController.h"
#include "SecretServicePasswordStore.h"
#include "SettingsStore.h"
#include "SystemLauncher.h"
#include "TrayController.h"

#include <QApplication>
#include <QIcon>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(easyspot_linux_client);
    QCoreApplication::setOrganizationName(QStringLiteral("EasySpot"));
    QCoreApplication::setApplicationName(QStringLiteral("EasySpot Linux Client"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/easyspot-linux-client.png")));
    QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<ControllerSnapshot>("ControllerSnapshot");

    SettingsStore settingsStore;
    SecretServicePasswordStore passwordStore;
    AutostartManager autostartManager;
    SystemLauncher systemLauncher;
    BleHotspotClient bleClient;
    NetworkManagerWifiController wifiController;

    HotspotWorkflowController controller(&bleClient,
                                         &wifiController,
                                         &passwordStore,
                                         &settingsStore,
                                         &autostartManager,
                                         &systemLauncher);
    MainWindow window(&controller);
    TrayController tray(&controller, &window);

    const bool startInTray = app.arguments().contains(QStringLiteral("--tray")) ||
                             app.arguments().contains(QStringLiteral("/tray"));
    if (!startInTray) {
        window.show();
    }
    tray.show();
    controller.initialize();

    return app.exec();
}
