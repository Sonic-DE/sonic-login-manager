# Options to allow packagers to choose which init scripts to install
# regardless of what is detected on the build machine
option(INSTALL_SYSTEMD "Install systemd unit files" OFF)
option(INSTALL_OPENRC "Install OpenRC init script" OFF)
option(INSTALL_BSD_RC "Install BSD rc.d init script" OFF)
option(INSTALL_SYSVINIT_RC "Install generic sysvinit/rc.d init script" OFF)
option(INSTALL_RUNIT "Install runit service files" OFF)
option(INSTALL_DINIT "Install dinit service files" OFF)
option(INSTALL_S6 "Install s6 service files" OFF)

# systemd service - install if detected or if explicitly requested via -DINSTALL_SYSTEMD=ON
if(SYSTEMD_FOUND OR INSTALL_SYSTEMD)
    configure_file(plasmalogin.service.in plasmalogin.service)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.service"
        DESTINATION "${KDE_INSTALL_SYSTEMDUNITDIR}/system"
    )

    configure_file(plasmalogin-sysuser.conf.in plasmalogin-sysuser.conf)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin-sysuser.conf"
        DESTINATION "lib/sysusers.d"
        RENAME plasmalogin.conf
    )
endif()

# OpenRC script - install if detected or if explicitly requested via -DINSTALL_OPENRC=ON
if(OPENRC_FOUND OR INSTALL_OPENRC)
    configure_file(plasmalogin.openrc plasmalogin.openrc @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.openrc"
        DESTINATION "/etc/init.d"
        RENAME plasmalogin
    )
endif()

# runit service - install if detected or if explicitly requested via -DINSTALL_RUNIT=ON
if(RUNIT_FOUND OR INSTALL_RUNIT)
    configure_file(plasmalogin.runit plasmalogin.runit @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.runit"
        DESTINATION "etc/sv/plasmalogin"
        RENAME run
    )
endif()

# dinit service - install if detected or if explicitly requested via -DINSTALL_DINIT=ON
if(DINIT_FOUND OR INSTALL_DINIT)
    configure_file(plasmalogin.dinit plasmalogin.dinit @ONLY)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.dinit"
        DESTINATION "/etc/dinit.d"
        RENAME plasmalogin
    )
endif()

# s6 service - install if detected or if explicitly requested via -DINSTALL_S6=ON
if(S6_FOUND OR INSTALL_S6)
    configure_file(plasmalogin.s6-run plasmalogin.s6-run @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.s6-run"
        DESTINATION "etc/s6/sv/plasmalogin"
        RENAME run
    )

    configure_file(plasmalogin.s6-finish plasmalogin.s6-finish @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.s6-finish"
        DESTINATION "etc/s6/sv/plasmalogin"
        RENAME finish
    )
endif()

# sysvinit script - install if detected or if explicitly requested via -DINSTALL_SYSVINIT_RC=ON
if(SYSVINIT_FOUND OR INSTALL_SYSVINIT_RC)
    configure_file(plasmalogin.sh.in plasmalogin.sh)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.sh"
        DESTINATION "etc/init.d"
        RENAME plasmalogin
    )

    configure_file(plasmalogin-tmpfiles.conf.in plasmalogin-tmpfiles.conf)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin-tmpfiles.conf"
        DESTINATION "lib/tmpfiles.d"
        RENAME plasmalogin.conf
    )
endif()

# BSD rc.d script - install if detected or if explicitly requested via -DINSTALL_BSD_RC=ON
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD" OR INSTALL_BSD_RC)
    configure_file(plasmalogin.sh.bsd.in plasmalogin.sh.bsd @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.sh.bsd"
        DESTINATION "etc/rc.d"
        RENAME plasmalogin
    )

    # Install Xwrapper.config for FreeBSD to allow plasmalogin user to start X server
    configure_file(Xwrapper.config.in Xwrapper.config)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/Xwrapper.config"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/X11"
    )
endif()

# Generic fallback - install only if no other init system is detected and not explicitly disabled
set(IS_BSD_SYSTEM CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD")
if(NOT SYSTEMD_FOUND AND NOT IS_BSD_SYSTEM AND NOT OPENRC_FOUND AND NOT RUNIT_FOUND AND NOT DINIT_FOUND AND NOT S6_FOUND AND NOT SYSVINIT_FOUND AND NOT INSTALL_SYSTEMD AND NOT INSTALL_OPENRC AND NOT INSTALL_RUNIT AND NOT INSTALL_DINIT AND NOT INSTALL_S6 AND NOT INSTALL_SYSVINIT_RC AND NOT INSTALL_BSD_RC)
    configure_file(plasmalogin.sh.in plasmalogin.sh)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.sh"
        DESTINATION "etc/rc.d"
        RENAME plasmalogin
    )

    configure_file(plasmalogin-tmpfiles.conf.in plasmalogin-tmpfiles.conf)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin-tmpfiles.conf"
        DESTINATION "lib/tmpfiles.d"
        RENAME plasmalogin.conf
    )
endif()

# systemd-tmpfiles can be used standalone without other systemd parts
if(DEFINED SYSTEMD_TMPFILES_DIR)
    configure_file(plasmalogin-tmpfiles.conf.in plasmalogin-tmpfiles.conf)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin-tmpfiles.conf"
        DESTINATION "lib/tmpfiles.d"
        RENAME plasmalogin.conf
    )
endif()
