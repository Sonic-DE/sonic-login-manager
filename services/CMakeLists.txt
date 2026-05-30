# Options to allow packagers to choose which init scripts to install
# regardless of what is detected on the build machine
include(GNUInstallDirs)
option(SPLIT_BUILD "Generate separate installation components for distros that support multiple init systems and split them in to separate packages" OFF)

# systemd service - install if detected
configure_file(systemd/soniclogin.service.in systemd/soniclogin.service)
configure_file(systemd/soniclogin.conf.in systemd/soniclogin.conf)
if(SPLIT_BUILD)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/systemd/soniclogin.service"
        DESTINATION "${KDE_INSTALL_SYSTEMDUNITDIR}/system"
        COMPONENT systemd
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/systemd/soniclogin.conf"
        DESTINATION "lib/sysusers.d"
        COMPONENT systemd
    )
elseif(SYSTEMD_FOUND)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/systemd/soniclogin.service"
        DESTINATION "${KDE_INSTALL_SYSTEMDUNITDIR}/system"
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/systemd/soniclogin.conf"
        DESTINATION "lib/sysusers.d"
    )
endif()

# OpenRC script - install if detected
configure_file(openrc/soniclogin.in openrc/soniclogin @ONLY)
if(SPLIT_BUILD)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/openrc/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/init.d"
        COMPONENT openrc
    )
elseif(OPENRC_FOUND)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/openrc/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/init.d"
    )
endif()

# runit service - install if detected
configure_file(runit/run.in runit/run @ONLY)
if(SPLIT_BUILD)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/runit/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/runit/sv/soniclogin"
        COMPONENT runit
    )
elseif(RUNIT_FOUND)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/runit/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/runit/sv/soniclogin"
    )
endif()

# dinit service - install if detected
configure_file(dinit/soniclogin.in dinit/soniclogin @ONLY)
if(SPLIT_BUILD)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/dinit/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/dinit.d"
        COMPONENT dinit
    )
elseif(DINIT_FOUND)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/dinit/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/dinit.d"
    )
endif()

# s6 service - install if detected
# Artix S6 convention: service split into soniclogin-srv and soniclogin-log
# soniclogin-srv contains the main daemon run script
# soniclogin-log is a no-op logger since soniclogin logs to its own file
configure_file(s6/run.in s6/run @ONLY)
configure_file(s6/log/run.in s6/log/run @ONLY)
if(SPLIT_BUILD)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/s6/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/type"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/producer-for"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/logind"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/mount-tmpfs"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/tmpfiles-setup"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/udevadm"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
        COMPONENT s6
    )

    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/s6/log/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
        RENAME "run"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/type"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/log/consumer-for"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
        COMPONENT s6
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/log/pipeline-name"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
        COMPONENT s6
    )
elseif(S6_FOUND)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/s6/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/type"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/producer-for"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/logind"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/mount-tmpfs"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/tmpfiles-setup"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/dependencies/udevadm"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-srv/dependencies.d"
    )

    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/s6/log/run"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/type"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/log/consumer-for"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/s6/log/pipeline-name"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/s6/sv/soniclogin-log"
    )
endif()

# sysvinit script - install if detected
configure_file(sysvinit/soniclogin.in sysvinit/soniclogin)
configure_file(sysvinit/soniclogin.conf.in sysvinit/soniclogin.conf)
if(SPLIT_BUILD)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/sysvinit/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/init.d"
        COMPONENT sysvinit
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/sysvinit/soniclogin.conf"
        DESTINATION "lib/tmpfiles.d"
        COMPONENT sysvinit
    )
elseif(SYSVINIT_FOUND)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/sysvinit/soniclogin"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/init.d"
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/sysvinit/soniclogin.conf"
        DESTINATION "lib/tmpfiles.d"
    )
endif()

# BSD rc.d script - install if detected
configure_file(bsd/soniclogin.in bsd/soniclogin @ONLY)
configure_file(bsd/Xwrapper.config.in bsd/Xwrapper.config)
if(SPLIT_BUILD)
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/bsd/soniclogin"
        DESTINATION "etc/rc.d"
        COMPONENT bsd
    )

    # Install Xwrapper.config for FreeBSD to allow soniclogin user to start X server
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/bsd/Xwrapper.config"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/X11"
        COMPONENT bsd
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD")
    install(
        PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/bsd/soniclogin"
        DESTINATION "etc/rc.d"
    )

    # Install Xwrapper.config for FreeBSD to allow soniclogin user to start X server
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/bsd/Xwrapper.config"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/X11"
    )
endif()


# systemd-tmpfiles can be used standalone without other systemd parts
configure_file(systemd/soniclogin.conf.in systemd/soniclogin.conf)
if(DEFINED SYSTEMD_TMPFILES_DIR)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/systemd/soniclogin.conf"
        DESTINATION "lib/tmpfiles.d"
    )
endif()
