# Options to allow packagers to choose which init scripts to install
# regardless of what is detected on the build machine
option(INSTALL_SYSTEMD "Install systemd unit files" OFF)
option(INSTALL_OPENRC "Install OpenRC init script" OFF)
option(INSTALL_BSD_RC "Install BSD rc.d init script" OFF)
option(INSTALL_SYSVINIT_RC "Install generic sysvinit/rc.d init script" OFF)

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

# OpenRC script - install only if explicitly requested via -DINSTALL_OPENRC=ON
if(INSTALL_OPENRC)
    configure_file(plasmalogin.openrc plasmalogin.openrc @ONLY)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/plasmalogin.openrc"
        DESTINATION "/etc/init.d"
        RENAME plasmalogin
    )
endif()

# BSD rc.d script - install if detected or if explicitly requested via -DINSTALL_BSD_RC=ON
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD" OR INSTALL_BSD_RC)
    configure_file(plasmalogin.sh.bsd.in plasmalogin.sh.bsd)
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

# Generic Linux rc.d script (sysvinit, etc.) - install if detected or if explicitly requested via -DINSTALL_SYSVINIT_RC=ON
set(IS_BSD_SYSTEM CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD")
if((NOT SYSTEMD_FOUND AND NOT IS_BSD_SYSTEM) OR INSTALL_SYSVINIT_RC)
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
