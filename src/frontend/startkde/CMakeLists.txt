if(SYSTEMD_FOUND)
    install(FILES soniclogin-x11.target DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
    ecm_install_configured_files(INPUT soniclogin-kwin_x11.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
    ecm_install_configured_files(INPUT soniclogin-apply-kscreen.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_BINARY_DIR}/src/common"
)

ecm_qt_declare_logging_category(start-soniclogin_SRCS HEADER debug.h IDENTIFIER PLASMA_STARTUP CATEGORY_NAME org.kde.startup)

add_executable(start-soniclogin-x11 start-soniclogin-x11.cpp start-soniclogin.cpp ${start-soniclogin_SRCS})

target_link_libraries(start-soniclogin-x11 PRIVATE
    Qt::Core
    Qt::Gui
    Qt::DBus
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::DBusAddons
    KF6::Package
    KF6::Screen
    PW::KLookAndFeel
)


kconfig_target_kcfg_file(start-soniclogin-x11
    FILE lookandfeelsettings.kcfg
    CLASS_NAME LookAndFeelSettings
)

install(TARGETS start-soniclogin-x11 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
