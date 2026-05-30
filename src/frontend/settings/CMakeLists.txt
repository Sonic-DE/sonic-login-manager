set(SONICLOGIN_CONFIG_FILE         "/etc/soniclogin.conf"                                    CACHE PATH  "Path of the soniclogin config file")
set(SONICLOGIN_SYSTEM_CONFIG_FILE  "${CMAKE_INSTALL_PREFIX}/lib/soniclogin/defaults.conf"    CACHE PATH  "Path of the system soniclogin config file")

configure_file(config.h.in config.h IMMEDIATE @ONLY)

# Covered by greeter/Messages.sh as we only have one string in here
add_definitions(-DTRANSLATION_DOMAIN="soniclogin")

add_library(settings STATIC
    config.h
    sonicloginsettings.cpp
    sonicloginsettingsdefaults.cpp
    wallpaperintegration.cpp
    wallpapersettings.cpp
    models/sessionmodel.cpp models/sessionmodel.h
    models/usermodel.cpp models/usermodel.h
)
set_property(TARGET settings PROPERTY POSITION_INDEPENDENT_CODE ON)

kconfig_add_kcfg_files(settings GENERATE_MOC sonicloginsettingsbase.kcfgc)
target_link_libraries(settings
    Qt6::Quick
    KF6::ConfigQml
    KF6::I18n
    KF6::Package
)
