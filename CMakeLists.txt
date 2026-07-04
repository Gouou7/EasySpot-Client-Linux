cmake_minimum_required(VERSION 3.24)

project(EasySpotLinuxClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Bluetooth DBus Test)

qt_standard_project_setup()

set(EASYSPOT_APP_SOURCES
    src/AppTypes.h
    src/AutostartManager.cpp
    src/AutostartManager.h
    src/BleHotspotClient.cpp
    src/BleHotspotClient.h
    src/CredentialsDialog.cpp
    src/CredentialsDialog.h
    src/HotspotWorkflowController.cpp
    src/HotspotWorkflowController.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/NetworkManagerWifiController.cpp
    src/NetworkManagerWifiController.h
    src/SecretServicePasswordStore.cpp
    src/SecretServicePasswordStore.h
    src/SettingsStore.cpp
    src/SettingsStore.h
    src/SystemLauncher.cpp
    src/SystemLauncher.h
    src/TrayController.cpp
    src/TrayController.h
    resources/easyspot-linux-client.qrc
    ui/CredentialsDialog.ui
    ui/MainWindow.ui
)

qt_add_library(easyspot_linux_core STATIC
    ${EASYSPOT_APP_SOURCES}
)

set_target_properties(easyspot_linux_core PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui"
)

target_include_directories(easyspot_linux_core PUBLIC src)
target_link_libraries(easyspot_linux_core PUBLIC
    Qt6::Bluetooth
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
)

qt_add_executable(easyspot-linux-client
    src/main.cpp
)

target_link_libraries(easyspot-linux-client PRIVATE easyspot_linux_core)

install(TARGETS easyspot-linux-client RUNTIME DESTINATION bin)
install(FILES resources/easyspot-linux-client.desktop DESTINATION share/applications)
install(FILES assets/EasySpotWindows-0.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/16x16/apps)
install(FILES assets/EasySpotWindows-2.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/24x24/apps)
install(FILES assets/EasySpotWindows-3.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/32x32/apps)
install(FILES assets/EasySpotWindows-5.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/48x48/apps)
install(FILES assets/EasySpotWindows-6.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/64x64/apps)
install(FILES assets/EasySpotWindows-7.png RENAME easyspot-linux-client.png DESTINATION share/icons/hicolor/256x256/apps)

enable_testing()
add_subdirectory(tests)
