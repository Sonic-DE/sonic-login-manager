# Include common directory for MessageHandler
include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_BINARY_DIR}/src/common"
)

add_executable(soniclogin-wallpaper)

add_compile_definitions(QT_MESSAGELOGCONTEXT)

ecm_add_qml_module(soniclogin-wallpaper URI "org.kde.sonic.login.wallpaper" GENERATE_PLUGIN_SOURCE)

target_sources(soniclogin-wallpaper PRIVATE
    main.cpp
    wallpaperapp.cpp
    wallpaperwindow.cpp
)

ecm_target_qml_sources(soniclogin-wallpaper PRIVATE
    SOURCES
    main.qml
    WallpaperFader.qml
)

target_link_libraries(soniclogin-wallpaper PRIVATE
    settings
    Qt::Core
    Qt::Gui
    Qt::DBus
    Qt::Quick
    KF6::WindowSystem
    KF6::Package
    Plasma::PlasmaQuick
)


qt_add_shaders(soniclogin-wallpaper "wallpaper_shaders"
    PRECOMPILE
    BATCHABLE
    OPTIMIZED
    PREFIX "/qt/qml/org/sonic/login/wallpaper/shaders"
    FILES
        WallpaperFader.frag
    OUTPUTS
        WallpaperFader.frag.qsb
)

ecm_finalize_qml_module(soniclogin-wallpaper)

install(TARGETS soniclogin-wallpaper DESTINATION ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
if(SYSTEMD_FOUND)
    ecm_install_configured_files(INPUT soniclogin-wallpaper.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()
