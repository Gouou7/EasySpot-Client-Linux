qt_add_executable(easyspot-linux-client-tests
    HotspotWorkflowControllerTests.cpp
)

target_link_libraries(easyspot-linux-client-tests PRIVATE
    easyspot_linux_core
    Qt6::Test
)

add_test(NAME easyspot-linux-client-tests COMMAND easyspot-linux-client-tests)

qt_add_executable(easyspot-secret-service-tests
    SecretServicePasswordStoreTests.cpp
)

target_link_libraries(easyspot-secret-service-tests PRIVATE
    easyspot_linux_core
    Qt6::Test
)

add_test(NAME easyspot-secret-service-tests COMMAND easyspot-secret-service-tests)

qt_add_executable(easyspot-network-manager-tests
    NetworkManagerWifiControllerTests.cpp
)

target_link_libraries(easyspot-network-manager-tests PRIVATE
    easyspot_linux_core
    Qt6::Test
)

add_test(NAME easyspot-network-manager-tests COMMAND easyspot-network-manager-tests)

qt_add_executable(easyspot-autostart-tests
    AutostartManagerTests.cpp
)

target_link_libraries(easyspot-autostart-tests PRIVATE
    easyspot_linux_core
    Qt6::Test
)

add_test(NAME easyspot-autostart-tests COMMAND easyspot-autostart-tests)

qt_add_executable(easyspot-main-window-tests
    MainWindowTests.cpp
)

target_link_libraries(easyspot-main-window-tests PRIVATE
    easyspot_linux_core
    Qt6::Test
)

add_test(NAME easyspot-main-window-tests COMMAND easyspot-main-window-tests)
set_tests_properties(easyspot-main-window-tests PROPERTIES
    ENVIRONMENT QT_QPA_PLATFORM=offscreen
)
