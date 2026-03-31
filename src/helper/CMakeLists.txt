include(CheckLibraryExists)

# On FreeBSD (possibly other BSDs as well), we want to use
# setusercontext() to set up the login configuration from login.conf
if("${CMAKE_SYSTEM_NAME}" STREQUAL "FreeBSD")
    find_library(_have_libutil util)
    if(_have_libutil)
        check_library_exists(${_have_libutil} setusercontext "" _have_setusercontext)
    endif()
    if(_have_libutil AND _have_setusercontext)
        target_link_libraries(plasmalogin-helper ${_have_libutil})
    endif()
endif()

include_directories(
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_SOURCE_DIR}/src/auth"
)
include_directories("${CMAKE_BINARY_DIR}/src/common")

add_executable(plasmalogin-helper)

target_sources(plasmalogin-helper PRIVATE
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

target_link_libraries(plasmalogin-helper
                      Qt6::Network
                      Qt6::DBus
                      Qt6::Qml)
if(LIBSYSTEMD_FOUND)
    target_link_libraries(plasmalogin-helper PkgConfig::LIBSYSTEMD)
endif()

target_link_libraries(plasmalogin-helper ${PAM_LIBRARIES})

# utmps for non-systemd session tracking
if (UTMPS_FOUND)
    target_include_directories(plasmalogin-helper SYSTEM PRIVATE ${UTMPS_INCLUDE_DIR})
    target_link_libraries(plasmalogin-helper ${UTMPS_LIBRARIES})
endif (UTMPS_FOUND)

install(TARGETS plasmalogin-helper RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")

add_executable(plasmalogin-helper-start-x11user HelperStartX11User.cpp xorguserhelper.cpp XAuth.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/ConfigReader.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/Configuration.cpp
                                                ${CMAKE_SOURCE_DIR}/src/common/SignalHandler.cpp
                                                )
target_link_libraries(plasmalogin-helper-start-x11user Qt6::Core PkgConfig::LIBXAU)
if(LIBSYSTEMD_FOUND)
    target_link_libraries(plasmalogin-helper-start-x11user PkgConfig::LIBSYSTEMD)
endif()
install(TARGETS plasmalogin-helper-start-x11user RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
