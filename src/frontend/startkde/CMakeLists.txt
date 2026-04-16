if(SYSTEMD_FOUND)
    install(FILES plasma-login-x11.target DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
    ecm_install_configured_files(INPUT plasma-login-kwin_x11.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_BINARY_DIR}/src/common"
)

ecm_qt_declare_logging_category(startplasma_SRCS HEADER debug.h IDENTIFIER PLASMA_STARTUP CATEGORY_NAME org.kde.startup)

add_executable(startplasma-login-x11 startplasmalogin-x11.cpp startplasma.cpp ${startplasma_SRCS})

# Only define HAVE_SYSTEMD when systemd is found - on non-systemd systems, don't define it at all
# This allows using #ifdef HAVE_SYSTEMD in the source code
if(SYSTEMD_FOUND)
    target_compile_definitions(startplasma-login-x11 PRIVATE HAVE_SYSTEMD)
endif()

target_link_libraries(startplasma-login-x11 PRIVATE
    Qt::Core
    Qt::DBus
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::DBusAddons
    KF6::Package
    PW::KLookAndFeel
)

kconfig_target_kcfg_file(startplasma-login-x11
    FILE lookandfeelsettings.kcfg
    CLASS_NAME LookAndFeelSettings
)

install(TARGETS startplasma-login-x11 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
