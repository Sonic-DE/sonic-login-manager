set(QT_USE_QTTEST TRUE)

include_directories(../src/common)

set(ConfigurationTest_SRCS ConfigurationTest.cpp ../src/common/ConfigReader.cpp)
add_executable(ConfigurationTest ${ConfigurationTest_SRCS})
add_test(NAME Configuration COMMAND ConfigurationTest)

configure_file(ConfigurationTest.conf ConfigurationTest.conf COPYONLY)
target_compile_definitions(ConfigurationTest PRIVATE TEST_CONF_FILE="${CMAKE_CURRENT_BINARY_DIR}/ConfigurationTest.conf")
set_tests_properties(Configuration PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(ConfigurationTest Qt6::Core Qt6::Test)

add_executable(FileDescriptorTest
    FileDescriptorTest.cpp
    ../src/common/filedescriptor.cpp
    ../src/common/VirtualTerminal.cpp
)
target_link_libraries(FileDescriptorTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME FileDescriptor COMMAND FileDescriptorTest)

add_executable(SessionModelTest
    SessionModelTest.cpp
    ../src/frontend/settings/models/sessionmodel.cpp
)
target_link_libraries(SessionModelTest PRIVATE Qt6::Core Qt6::Test KF6::ConfigCore KF6::I18n)
add_test(NAME SessionModel COMMAND SessionModelTest)

add_executable(GreeterProtocolTest
    GreeterProtocolTest.cpp
    ../src/frontend/greeter/backend/GreeterProxy.cpp
    ../src/common/SocketWriter.cpp
    ../src/common/Session.cpp
)
target_include_directories(GreeterProtocolTest PRIVATE ../src/frontend/greeter/backend)
target_link_libraries(GreeterProtocolTest PRIVATE Qt6::Core Qt6::Network Qt6::Test KF6::ConfigCore)
add_test(NAME GreeterProtocol COMMAND GreeterProtocolTest)

add_executable(GreeterEventFilterTest
    GreeterEventFilterTest.cpp
    ../src/frontend/greeter/greetereventfilter.cpp
)
target_link_libraries(GreeterEventFilterTest PRIVATE Qt6::Quick Qt6::Test KF6::ScreenDpms)
add_test(NAME GreeterEventFilter COMMAND GreeterEventFilterTest)
# Do not access the real display or its DPMS service during the Escape-key test.
set_tests_properties(GreeterEventFilter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/soniclogin-test-bus"
)

add_test(
    NAME FindNativeXServer
    COMMAND ${CMAKE_COMMAND}
        -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/FindNativeXServerTest.cmake
)

# set(SessionTest_SRCS SessionTest.cpp ../src/common/Configuration.cpp ../src/common/ConfigReader.cpp ../src/common/Session.cpp)
# add_executable(SessionTest ${SessionTest_SRCS})
# target_include_directories(SessionTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../src/common)
# add_test(NAME Session COMMAND SessionTest)
# target_link_libraries(SessionTest Qt6::Core Qt6::Test)
