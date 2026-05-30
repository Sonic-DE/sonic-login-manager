# Due to copy pasted code from QtGreet
remove_definitions(-DQT_NO_CAST_FROM_ASCII)
remove_definitions(-DQT_NO_CAST_FROM_BYTEARRAY=0)
remove_definitions(-DQT_USE_QSTRINGBUILDER)

# Include common directory
include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_BINARY_DIR}/src/common"
)

set(LOGIN_GREETER_SRCS
    main.cpp
    stateconfig.cpp
    backend/GreeterProxy.cpp
    backend/SocketWriter.cpp
    mockbackend/MockGreeterProxy.cpp
    blurscreenbridge.cpp
    greetereventfilter.cpp
    ${CMAKE_SOURCE_DIR}/src/common/SignalHandler.cpp
)

add_definitions(-DTRANSLATION_DOMAIN="soniclogin")

ecm_qt_declare_logging_category(LOGIN_GREETER_SRCS HEADER debug.h
                                IDENTIFIER LOGOUT_GREETER
                                CATEGORY_NAME kde.login_greeter
                                DEFAULT_SEVERITY Debug
                                DESCRIPTION "login greeter debug"
                                EXPORT PLASMAWORKSPACE
                            )

add_executable(soniclogin-greeter ${LOGIN_GREETER_SRCS})

# Enable automatic MOC processing for Qt signals/slots
set_target_properties(soniclogin-greeter PROPERTIES AUTOMOC ON)


set_source_files_properties(qml/GreeterState.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

ecm_add_qml_module(soniclogin-greeter URI "org.kde.sonic.login" GENERATE_PLUGIN_SOURCE)
ecm_target_qml_sources(soniclogin-greeter
    SOURCES
    qml/Main.qml
    qml/Login.qml
    qml/SessionButton.qml
    qml/KeyboardButton.qml
    qml/GreeterState.qml

    RESOURCES
    qml/faces/.face.icon
)

kconfig_add_kcfg_files(soniclogin-greeter GENERATE_MOC stateconfig.kcfgc)

target_link_libraries(soniclogin-greeter PRIVATE
    settings
    Qt::Quick
    Qt::DBus
    KF6::I18n
    Plasma::PlasmaQuick
    KF6::Package
    KF6::WindowSystem
    KF6::ConfigCore
    X11::X11
    PW::KWorkspace
    KF6::ScreenDpms
)


ecm_finalize_qml_module(soniclogin-greeter)

install(TARGETS soniclogin-greeter DESTINATION ${KDE_INSTALL_LIBEXECDIR})

if(SYSTEMD_FOUND)
    ecm_install_configured_files(INPUT soniclogin.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

ki18n_install(po)
