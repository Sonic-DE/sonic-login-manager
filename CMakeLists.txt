cmake_minimum_required(VERSION 3.22)

set(PROJECT_VERSION "6.7.0")

project(SONICLOGIN VERSION ${PROJECT_VERSION})

# Options
option(INSTALL_PAM_CONFIGURATION "Install PAM configuration files" ON)

# Version settings
set(PROJECT_DEP_VERSION "6.6.91")

set(QT_MIN_VERSION "6.10.0")
set(KF6_MIN_VERSION "6.26.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.82")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Include KDE CMake modules
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMMarkNonGuiExecutable)
include(CMakePackageConfigHelpers)
include(FeatureSummary)
include(GenerateExportHeader)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(ECMQtDeclareLoggingCategory)
include(ECMConfiguredInstall)
include(ECMQmlModule)

find_package(Qt6 CONFIG REQUIRED Core DBus Gui Qml Quick LinguistTools Test QuickTest ShaderTools)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Config Package WindowSystem I18n DBusAddons KCMUtils Auth KIO)
find_package(PlasmaQuick ${PROJECT_DEP_VERSION} REQUIRED)
find_package(LibKWorkspace ${PROJECT_DEP_VERSION} REQUIRED)
find_package(LibKLookAndFeel ${PROJECT_DEP_VERSION} REQUIRED)
find_package(KF6Screen ${PROJECT_DEP_VERSION} REQUIRED)

# PAM
find_package(PAM REQUIRED)

# PKG-CONFIG
find_package(PkgConfig)

# XAU
pkg_check_modules(LIBXAU REQUIRED "xau" IMPORTED_TARGET)

# Check for systemd to get unit file install directories (runtime detection is used in code)
pkg_check_modules(SYSTEMD "systemd")
if(SYSTEMD_FOUND)
    if (NOT DEFINED SYSTEMD_SYSTEM_UNIT_DIR)
        pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd systemdsystemunitdir)
    endif()

    if (NOT DEFINED SYSTEMD_SYSUSERS_DIR)
        pkg_get_variable(SYSTEMD_SYSUSERS_DIR systemd sysusersdir)
    endif()

    if (NOT DEFINED SYSTEMD_TMPFILES_DIR)
        pkg_get_variable(SYSTEMD_TMPFILES_DIR systemd tmpfilesdir)
    endif()
endif()

# Auto-detect init systems for automatic installation
# OpenRC detection
find_program(OPENRC_OPENRC_EXECUTABLE NAMES openrc)
find_program(OPENRC_RC_EXECUTABLE NAMES rc)
if(OPENRC_OPENRC_EXECUTABLE OR OPENRC_RC_EXECUTABLE)
    set(OPENRC_FOUND TRUE)
else()
    set(OPENRC_FOUND FALSE)
endif()

# runit detection
find_program(RUNIT_RUNSVDIR_EXECUTABLE NAMES runsvdir)
if(RUNIT_RUNSVDIR_EXECUTABLE)
    set(RUNIT_FOUND TRUE)
else()
    set(RUNIT_FOUND FALSE)
endif()

# dinit detection
find_program(DINIT_DAEMON_EXECUTABLE NAMES dinit)
if(DINIT_DAEMON_EXECUTABLE)
    set(DINIT_FOUND TRUE)
else()
    set(DINIT_FOUND FALSE)
endif()

# s6 detection
find_program(S6_SVSCAN_EXECUTABLE NAMES s6-svscan)
if(S6_SVSCAN_EXECUTABLE)
    set(S6_FOUND TRUE)
else()
    set(S6_FOUND FALSE)
endif()

# sysvinit detection - check for /etc/inittab and telinit command
if(EXISTS "/etc/inittab")
    find_program(SYSVINIT_TELINIT_EXECUTABLE NAMES telinit)
    if(SYSVINIT_TELINIT_EXECUTABLE)
        set(SYSVINIT_FOUND TRUE)
    else()
        set(SYSVINIT_FOUND FALSE)
    endif()
else()
    set(SYSVINIT_FOUND FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "GNU/kFreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR CMAKE_SYSTEM_NAME STREQUAL "DragonFly" OR CMAKE_SYSTEM_NAME STREQUAL "GhostBSD")
    set(RUNTIME_DIR_DEFAULT "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/run/soniclogin")
else()
    set(RUNTIME_DIR_DEFAULT "/run/soniclogin")
endif()

# utmps - for session tracking on non-systemd systems
find_package(utmps)
add_feature_info("utmps" UTMPS_FOUND "utmps support for non-systemd session tracking")

# Extra default install directories
set(PAM_CONFIG_DIR "${CMAKE_INSTALL_SYSCONFDIR}/pam.d" CACHE PATH "Install directory for PAM configuration")

# Set constants
set(DATA_INSTALL_DIR            "${CMAKE_INSTALL_FULL_DATADIR}/soniclogin"                     CACHE PATH      "System application data install directory")
set(DBUS_CONFIG_DIR             "${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/system.d"                 CACHE PATH      "DBus config files directory")
set(STATE_DIR                   "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/soniclogin"           CACHE PATH      "State directory")
set(RUNTIME_DIR                 "${RUNTIME_DIR_DEFAULT}"                                        CACHE PATH      "Runtime data storage directory")

set(SESSION_COMMAND             "${DATA_INSTALL_DIR}/scripts/Xsession"                          CACHE PATH      "Script to execute when starting the X11 desktop session")
set(WAYLAND_SESSION_COMMAND     "${DATA_INSTALL_DIR}/scripts/wayland-session"                   CACHE PATH      "Script to execute when starting the Wayland desktop session")

set(CONFIG_FILE                 "${CMAKE_INSTALL_FULL_SYSCONFDIR}/soniclogin.conf"             CACHE PATH      "Path of the soniclogin config file")
set(CONFIG_DIR                  "${CMAKE_INSTALL_FULL_SYSCONFDIR}/soniclogin.conf.d"           CACHE PATH      "Path of the soniclogin config directory")
set(SYSTEM_CONFIG_DIR           "${CMAKE_INSTALL_PREFIX}/lib/soniclogin/soniclogin.conf.d"    CACHE PATH      "Path of the system soniclogin config directory")
set(LOG_FILE                    "/var/log/sonic/loginmanager.log"                               CACHE PATH      "Path of the soniclogin log file")
set(DBUS_CONFIG_FILENAME        "org.freedesktop.DisplayManager.conf"                           CACHE STRING    "Name of the soniclogin config file")
set(SONICLOGIN_INITIAL_VT       "1"                                                             CACHE STRING    "Initial tty to use")

if(NOT DEFINED LOGIN_DEFS_PATH)
    set(LOGIN_DEFS_PATH "/etc/login.defs" CACHE PATH "Path to login.defs")
endif()

# Add subdirectories
add_subdirectory(data)
add_subdirectory(services)
add_subdirectory(src)

add_subdirectory(test)

ki18n_install(po)

# Display feature summary
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

# CPack split-package generation for init-system-specific subpackages
# When SPLIT_BUILD is ON, each init system is tagged with a COMPONENT.
# Run: cmake -B build -DSPLIT_BUILD=ON && cmake --build build && cpack -G TGZ
# This produces individual .tar.gz packages per init system (e.g. soniclogin-s6, soniclogin-systemd, etc.)
include(CPack)
set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h)
list(FILTER ALL_CLANG_FORMAT_SOURCE_FILES EXCLUDE REGEX ".*\\.ccls-cache/.*")
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)


