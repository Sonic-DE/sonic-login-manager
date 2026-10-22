set(QT_USE_QTTEST TRUE)

include_directories(../src/common)

set(ConfigurationTest_SRCS ConfigurationTest.cpp ../src/common/ConfigReader.cpp)
add_executable(ConfigurationTest ${ConfigurationTest_SRCS})
add_test(NAME Configuration COMMAND ConfigurationTest)

configure_file(ConfigurationTest.conf ConfigurationTest.conf COPYONLY)
target_compile_definitions(ConfigurationTest PRIVATE TEST_CONF_FILE="${CMAKE_CURRENT_BINARY_DIR}/ConfigurationTest.conf")
set_tests_properties(Configuration PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(ConfigurationTest Qt6::Core Qt6::Test)

find_package(X11 REQUIRED COMPONENTS Xcursor)
add_executable(ThemeSyncTest ThemeSyncTest.cpp ../src/common/ThemeSync.cpp)
target_link_libraries(ThemeSyncTest PRIVATE Qt6::Test KF6::ConfigCore ZLIB::ZLIB X11::Xcursor)
add_test(NAME ThemeSync COMMAND ThemeSyncTest)

find_package(Qt6 REQUIRED COMPONENTS QuickControls2)
add_executable(GreeterPaletteTest GreeterPaletteTest.cpp)
target_link_libraries(GreeterPaletteTest PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test KF6::IconThemes KF6::I18nQml)
target_compile_definitions(GreeterPaletteTest PRIVATE GREETER_PALETTE_QML="${CMAKE_CURRENT_SOURCE_DIR}/GreeterPaletteTest.qml")
qt_add_resources(GreeterPaletteTest greeter_palette_config
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/src/frontend/greeter"
    FILES "${CMAKE_SOURCE_DIR}/src/frontend/greeter/qtquickcontrols2.conf"
)
add_test(NAME GreeterPalette COMMAND GreeterPaletteTest)
set_tests_properties(GreeterPalette PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-bus;DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-system-bus"
)

# Compile the production loader with private settings and no system sources. Never
# read /etc/soniclogin.conf or write the system log during an automated test.
set(wallpaper_source "${CMAKE_SOURCE_DIR}/src/frontend/wallpaper")
set(settings_source "${CMAKE_SOURCE_DIR}/src/frontend/settings")
add_executable(WallpaperAppTest
    WallpaperAppTest.cpp
    ${wallpaper_source}/wallpaperapp.cpp
    ${wallpaper_source}/wallpaperwindow.cpp
    ${settings_source}/sonicloginsettings.cpp
    ${settings_source}/sonicloginsettingsdefaults.cpp
    ${settings_source}/sonicloginsettings.h
    ${settings_source}/sonicloginsettingsdefaults.h
)
target_include_directories(WallpaperAppTest BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/wallpaper" "${wallpaper_source}" "${settings_source}"
)
target_compile_definitions(WallpaperAppTest PRIVATE WALLPAPER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/wallpaper")
kconfig_add_kcfg_files(WallpaperAppTest GENERATE_MOC ${settings_source}/sonicloginsettingsbase.kcfgc)
target_link_libraries(WallpaperAppTest PRIVATE Qt6::Quick Qt6::Network Qt6::DBus Qt6::Test
    KF6::ConfigQml KF6::I18n KF6::Package KF6::WindowSystem Plasma::PlasmaQuick)
qt_add_resources(WallpaperAppTest wallpaper_test_host
    PREFIX "/qt/qml/org/kde/sonic/login/wallpaper"
    BASE "${wallpaper_source}"
    FILES ${wallpaper_source}/main.qml ${wallpaper_source}/WallpaperFader.qml
)
qt_add_resources(WallpaperAppTest wallpaper_test_module
    PREFIX "/qt/qml/org/kde/sonic/login/wallpaper"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/wallpaper"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/wallpaper/qmldir"
)
qt_add_shaders(WallpaperAppTest wallpaper_test_shaders
    PREFIX "/qt/qml/org/sonic/login/wallpaper/shaders"
    FILES ${wallpaper_source}/WallpaperFader.frag
    OUTPUTS WallpaperFader.frag.qsb
)
add_test(NAME WallpaperApp COMMAND WallpaperAppTest)
set_tests_properties(WallpaperApp PROPERTIES TIMEOUT 120 ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-bus;DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-system-bus"
)

add_executable(FileDescriptorTest
    FileDescriptorTest.cpp
    ../src/common/filedescriptor.cpp
    ../src/common/VirtualTerminal.cpp
)
target_link_libraries(FileDescriptorTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME FileDescriptor COMMAND FileDescriptorTest)

add_executable(SessionModelTest
    SessionModelTest.cpp
    ../src/frontend/settings/models/sessionmodel.cpp
)
target_link_libraries(SessionModelTest PRIVATE Qt6::Core Qt6::Test KF6::ConfigCore KF6::I18n)
add_test(NAME SessionModel COMMAND SessionModelTest)

add_executable(GreeterProtocolTest
    GreeterProtocolTest.cpp
    ../src/frontend/greeter/backend/GreeterProxy.cpp
    ../src/common/SocketWriter.cpp
    ../src/common/Session.cpp
)
target_include_directories(GreeterProtocolTest PRIVATE ../src/frontend/greeter/backend)
target_link_libraries(GreeterProtocolTest PRIVATE Qt6::Core Qt6::Network Qt6::Test KF6::ConfigCore)
add_test(NAME GreeterProtocol COMMAND GreeterProtocolTest)

add_executable(GreeterEventFilterTest
    GreeterEventFilterTest.cpp
    ../src/frontend/greeter/greetereventfilter.cpp
)
target_link_libraries(GreeterEventFilterTest PRIVATE Qt6::Quick Qt6::Test KF6::ScreenDpms)
add_test(NAME GreeterEventFilter COMMAND GreeterEventFilterTest)
# Do not access the real display or its DPMS service during the Escape-key test.
set_tests_properties(GreeterEventFilter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-bus"
)

add_test(
    NAME FindNativeXServer
    COMMAND ${CMAKE_COMMAND}
        -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/FindNativeXServerTest.cmake
)

# set(SessionTest_SRCS SessionTest.cpp ../src/common/Configuration.cpp ../src/common/ConfigReader.cpp ../src/common/Session.cpp)
# add_executable(SessionTest ${SessionTest_SRCS})
# target_include_directories(SessionTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../src/common)
# add_test(NAME Session COMMAND SessionTest)
# target_link_libraries(SessionTest Qt6::Core Qt6::Test)
