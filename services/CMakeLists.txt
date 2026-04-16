if(SYSTEMD_FOUND)
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

# non-systemd rc.d script
if(NOT SYSTEMD_FOUND)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD")
        configure_file(plasmalogin.sh.bsd.in plasmalogin.sh)
    else()
        configure_file(plasmalogin.sh.in plasmalogin.sh)
    endif()
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
