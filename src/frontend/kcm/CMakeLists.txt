kcmutils_add_qml_kcm(kcm_soniclogin)
add_definitions(-DTRANSLATION_DOMAIN="kcm_soniclogin")
add_compile_definitions(QT_MESSAGELOGCONTEXT)

target_sources(kcm_soniclogin PRIVATE
    kcm.cpp kcm.h
    kcm_messagelogger.cpp
    soniclogindata.cpp soniclogindata.h
)

target_include_directories(kcm_soniclogin PRIVATE
    "${CMAKE_SOURCE_DIR}/src/common"
)

target_link_libraries(kcm_soniclogin PRIVATE
    settings
    KF6::I18n
    KF6::KCMUtilsCore
    KF6::KCMUtilsQuick
    KF6::KIOGui
    KF6::Service
    Qt6::Core
    Qt6::Network
)

ki18n_install(po)
