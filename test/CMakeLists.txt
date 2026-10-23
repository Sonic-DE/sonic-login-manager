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
target_include_directories(GreeterPaletteTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(GreeterPaletteTest PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test KF6::IconThemes KF6::I18nQml KF6::ConfigGui)
kconfig_target_kcfg_file(GreeterPaletteTest
    FILE ../src/frontend/startkde/lookandfeelsettings.kcfg
    CLASS_NAME LookAndFeelSettings
)
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

# Execute the real startup code with private constants and an init detector
# stub; never launch a real compositor, greeter, daemon or authentication flow.
set(CURSOR_STARTUP_STATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/cursor-startup-state")
set(CURSOR_STARTUP_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/cursor-startup-bin")
find_package(Qt6GuiPrivate REQUIRED)
configure_file(cursor-startup/Constants.h.in cursor-startup/Constants.h @ONLY)
ecm_qt_declare_logging_category(cursor_startup_sources HEADER debug.h
    IDENTIFIER PLASMA_STARTUP CATEGORY_NAME org.kde.startup DEFAULT_SEVERITY Debug)
add_executable(CursorStartupHelper ../src/frontend/startkde/start-soniclogin.cpp ${cursor_startup_sources})
get_target_property(cursor_startup_libraries start-soniclogin-x11 LINK_LIBRARIES)
target_link_libraries(CursorStartupHelper PRIVATE ${cursor_startup_libraries})
target_include_directories(CursorStartupHelper BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/cursor-startup" "${CMAKE_CURRENT_SOURCE_DIR}/cursor-startup")
kconfig_target_kcfg_file(CursorStartupHelper
    FILE "${CMAKE_SOURCE_DIR}/src/frontend/startkde/lookandfeelsettings.kcfg" CLASS_NAME LookAndFeelSettings)

find_package(X11 REQUIRED COMPONENTS Xcursor Xfixes)
add_executable(CursorStartupTest CursorStartupTest.cpp)
target_link_libraries(CursorStartupTest PRIVATE Qt6::Test Qt6::DBus X11::Xcursor X11::Xfixes)
target_compile_definitions(CursorStartupTest PRIVATE
    CURSOR_STARTUP_STATE_DIR="${CURSOR_STARTUP_STATE_DIR}"
    CURSOR_STARTUP_BIN_DIR="${CURSOR_STARTUP_BIN_DIR}"
    CURSOR_STARTUP_HELPER="$<TARGET_FILE:CursorStartupHelper>")
add_dependencies(CursorStartupTest CursorStartupHelper)
find_program(XVFB_RUN_EXECUTABLE xvfb-run)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(XVFB_RUN_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME CursorStartup COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 800x600x24"
        ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_COMMAND} -E env
        QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=generic QT_NO_XDG_DESKTOP_PORTAL=1
        SONICLOGIN_CURSOR_TEST_ISOLATED=1 $<TARGET_FILE:CursorStartupTest>)
else()
    message(STATUS "CursorStartup X11 integration test needs xvfb-run and dbus-run-session; integration cases will be skipped")
    add_test(NAME CursorStartup COMMAND CursorStartupTest)
endif()
set_tests_properties(CursorStartup PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
add_test(NAME CursorSystemdOrdering COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/CursorSystemdOrderingTest.cmake)

add_executable(WallpaperFaderTest WallpaperFaderTest.cpp)
target_link_libraries(WallpaperFaderTest PRIVATE Qt6::Quick Qt6::Test)
target_compile_definitions(WallpaperFaderTest PRIVATE WALLPAPER_FADER_TEST_QML="${CMAKE_CURRENT_SOURCE_DIR}/WallpaperFaderTest.qml")
qt_add_shaders(WallpaperFaderTest wallpaper_fader_test_shaders
    PREFIX "/qt/qml/org/sonic/login/wallpaper/shaders"
    FILES ${wallpaper_source}/WallpaperFader.frag OUTPUTS WallpaperFader.frag.qsb)
if(XVFB_RUN_EXECUTABLE)
    add_test(NAME WallpaperFader COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1920x1080x24"
        ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl
        LIBGL_ALWAYS_SOFTWARE=1 SONICLOGIN_BLUR_TEST_ISOLATED=1 $<TARGET_FILE:WallpaperFaderTest>)
else()
    add_test(NAME WallpaperFader COMMAND WallpaperFaderTest)
    set_tests_properties(WallpaperFader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(WallpaperFader PROPERTIES TIMEOUT 90)

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
