include(CheckLibraryExists)

include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_SOURCE_DIR}/src/auth"
)
include_directories("${CMAKE_BINARY_DIR}/src/common")

add_executable(soniclogin-helper)

target_sources(soniclogin-helper PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common/Configuration.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ConfigReader.cpp
    ${CMAKE_SOURCE_DIR}/src/common/SafeDataStream.cpp
    ${CMAKE_SOURCE_DIR}/src/common/SignalHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/Auth.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/AuthRequest.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/AuthPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/common/VirtualTerminal.cpp
    HelperApp.cpp
    UserSession.cpp
    backend/PamHandle.cpp
    backend/PamBackend.cpp
)

target_link_libraries(soniclogin-helper
                      Qt6::Network
                      Qt6::DBus
                      Qt6::Qml)

target_link_libraries(soniclogin-helper ${PAM_LIBRARIES})

# libcap for ambient capabilities (CAP_SYS_TTY_CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAP REQUIRED libcap)
target_include_directories(soniclogin-helper PRIVATE ${LIBCAP_INCLUDE_DIRS})
target_link_libraries(soniclogin-helper ${LIBCAP_LIBRARIES})

# On FreeBSD (possibly other BSDs as well), we want to use
# setusercontext() to set up the login configuration from login.conf
# Link util library for setusercontext on FreeBSD/BSD
if("${CMAKE_SYSTEM_NAME}" STREQUAL "FreeBSD" OR "${CMAKE_SYSTEM_NAME}" STREQUAL "GhostBSD")
    find_library(_have_libutil util)
    if(_have_libutil)
        check_library_exists(${_have_libutil} setusercontext "" _have_setusercontext)
    endif()
    if(_have_libutil AND _have_setusercontext)
        target_link_libraries(soniclogin-helper ${_have_libutil})
    endif()
endif()

# utmps for non-systemd session tracking
if (UTMPS_FOUND)
    target_include_directories(soniclogin-helper SYSTEM PRIVATE ${UTMPS_INCLUDE_DIR})
    target_link_libraries(soniclogin-helper ${UTMPS_LIBRARIES})
endif (UTMPS_FOUND)

install(TARGETS soniclogin-helper RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")

add_executable(soniclogin-helper-start-x11user HelperStartX11User.cpp xorguserhelper.cpp XAuth.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/ConfigReader.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/Configuration.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/SignalHandler.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/VirtualTerminal.cpp
                                                )
target_link_libraries(soniclogin-helper-start-x11user Qt6::Core Qt6::DBus PkgConfig::LIBXAU ${LIBCAP_LIBRARIES})


install(TARGETS soniclogin-helper-start-x11user RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
