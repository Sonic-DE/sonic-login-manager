kauth_install_actions(org.kde.kcontrol.kcmsoniclogin kcm_soniclogin.actions)

add_executable(kcmsoniclogin_authhelper sonicloginauthhelper.cpp sonicloginauthhelper.h)
target_link_libraries(kcmsoniclogin_authhelper settings KF6::AuthCore KF6::ConfigCore KF6::I18n Qt6::DBus)

kauth_install_helper_files(kcmsoniclogin_authhelper org.kde.kcontrol.kcmsoniclogin root)
install(TARGETS kcmsoniclogin_authhelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
